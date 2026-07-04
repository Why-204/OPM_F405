/**
 * @file    hmi_user_uart.c
 * @brief   HMI UART abstraction — IDLE+DMA RX, ISR-driven TX,
 *          RTOS unpack thread, event queue.
 *
 * Architecture (aligned with rm2023cbf-sentry communicate pattern):
 *
 *   [UART2 IDLE ISR] → DMA buffer → cmd_queue ring buffer
 *                                          ↓
 *                              hmi_uart_unpack_task()
 *                                          ↓
 *                              hmi_driver_unpack() → HmiEventQueue
 *                                          ↓
 *                              consumer hmi_event_pop()
 */

#include "hmi_user_uart.h"
#include "cmd_queue.h"
#include "hmi_driver.h"
#include <string.h>

/* ================================================================ */
/*  Static globals                                                   */
/* ================================================================ */

extern UART_HandleTypeDef hmi_huart;

/* ---- RX DMA buffer (CIRCULAR, DMA writes continuously) ---------- */
static uint8_t rx_dma_buf[HMI_RX_DMA_BUF_SIZE];
static uint16_t dma_rd_pos; /* last consumed NDTR-based position */

/* ---- RX ring buffer (cmd_queue, ISR→thread bridge) -------------- */
static HmiRxRingBuf rx_ring;

/* ---- Event queue ------------------------------------------------- */
static HmiEventQueue evt_queue;
static osMutexId_t evt_mutex; /* protect consumer access to evt_queue */

/* ---- TX DMA synchronization -------------------------------------- */
static osMutexId_t tx_mutex;
static osThreadId_t tx_wait_thread;

/* ---- RTOS objects ------------------------------------------------ */
static osThreadId_t unpack_thread_id;
#define HMI_UART_RX_RDY 0x0001U /* event flag: new DMA data ready */
#define HMI_UART_TX_DONE 0x0002U
#define HMI_UART_TX_ERROR 0x0004U
#define HMI_UART_TX_FLAGS (HMI_UART_TX_DONE | HMI_UART_TX_ERROR)
#define HMI_UART_TX_TIMEOUT_MS 1000U

/* ================================================================ */
/*  Forward declarations                                             */
/* ================================================================ */
static void hmi_uart_unpack_task(void *arg);

static const osMutexAttr_t hmi_evt_mutex_attr = {
    .name = "hmi_evt_mutex",
};

static const osMutexAttr_t hmi_tx_mutex_attr = {
    .name = "hmi_tx_mutex",
};

static const osThreadAttr_t hmi_unpack_thread_attr = {
    .name = "hmi_unpack",
    .stack_size = 512U,
    .priority = (osPriority_t)osPriorityNormal,
};

/* ================================================================ */
/*  Event queue helpers (mutex-protected)                            */
/* ================================================================ */

bool hmi_event_push(const HmiEvent *evt)
{
    if (evt_mutex == NULL)
    {
        return false;
    }
    osMutexAcquire(evt_mutex, osWaitForever);
    uint16_t next = (evt_queue.head + 1U) % HMI_EVENT_QUEUE_SIZE;
    bool ok = false;
    if (next != evt_queue.tail)
    {
        memcpy(&evt_queue.events[evt_queue.head], evt, sizeof(HmiEvent));
        evt_queue.head = next;
        ok = true;
    }
    osMutexRelease(evt_mutex);
    return ok;
}

bool hmi_event_pop(HmiEvent *evt)
{
    if (evt_mutex == NULL)
    {
        return false;
    }
    osMutexAcquire(evt_mutex, osWaitForever);
    bool ok = false;
    if (evt_queue.tail != evt_queue.head)
    {
        memcpy(evt, &evt_queue.events[evt_queue.tail], sizeof(HmiEvent));
        evt_queue.tail = (evt_queue.tail + 1U) % HMI_EVENT_QUEUE_SIZE;
        ok = true;
    }
    osMutexRelease(evt_mutex);
    return ok;
}

uint16_t hmi_event_available(void)
{
    if (evt_mutex == NULL)
    {
        return 0;
    }
    osMutexAcquire(evt_mutex, osWaitForever);
    uint16_t n;
    if (evt_queue.head >= evt_queue.tail)
        n = evt_queue.head - evt_queue.tail;
    else
        n = HMI_EVENT_QUEUE_SIZE - evt_queue.tail + evt_queue.head;
    osMutexRelease(evt_mutex);
    return n;
}

/* ================================================================ */
/*  UART init                                                        */
/* ================================================================ */

void hmi_uart_init(void)
{
    /* zero-initialise all static state */
    memset(&rx_ring, 0, sizeof(rx_ring));
    memset(&evt_queue, 0, sizeof(evt_queue));
    dma_rd_pos = 0U;
    tx_wait_thread = NULL;

    /* ── Convert RX DMA to CIRCULAR mode for gapless reception ──
     * CubeMX configured NORMAL; we override here so DMA never
     * stops — no manual restart, no data lost between IDLE. */
    hmi_huart.hdmarx->Instance->CR |= DMA_SxCR_CIRC;

    /* Start IDLE+DMA reception */
    __HAL_UART_CLEAR_IDLEFLAG(&hmi_huart);
    __HAL_UART_ENABLE_IT(&hmi_huart, UART_IT_IDLE);
    HAL_UART_Receive_DMA(&hmi_huart, rx_dma_buf, HMI_RX_DMA_BUF_SIZE);

    /* ── Disable DMA TC / HT interrupts ────────────────────────
     * HAL_UART_Receive_DMA registered UART_DMAReceiveCplt as the
     * TC callback.  In CIRCULAR mode that callback fires on every
     * NDTR wrap and clears CR3_DMAR — killing RX permanently.
     * We suppress the interrupt source so the callback never runs. */
    __HAL_DMA_DISABLE_IT(hmi_huart.hdmarx, DMA_IT_TC | DMA_IT_HT);
}

/* ================================================================ */
/*  IDLE ISR entry (called from USART2_IRQHandler)                   */
/* ================================================================ */
void hmi_uart_idle_isr(UART_HandleTypeDef *huart)
{
    if (huart->Instance != hmi_huart.Instance)
        return;

    /* ── Clear UART error flags (ORE / FE / NE) so reception
     * never stalls due to a past noise glitch. ─────────────── */
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE))
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
    }

    if (!(__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE)))
        return;
    __HAL_UART_CLEAR_IDLEFLAG(huart);

    /* ── CIRCULAR DMA: NDTR = bytes remaining until next wrap.
     * Current DMA write position = buf_size − NDTR.
     * No DMA stop/restart needed — DMA runs forever. ──────── */
    uint16_t ndtr = __HAL_DMA_GET_COUNTER(huart->hdmarx);
    uint16_t wr_pos = HMI_RX_DMA_BUF_SIZE - ndtr;

    uint16_t len;
    if (wr_pos >= dma_rd_pos)
    {
        len = wr_pos - dma_rd_pos; /* contiguous */
    }
    else
    {
        len = HMI_RX_DMA_BUF_SIZE - dma_rd_pos + wr_pos; /* wrapped */
    }

    /* Sanity: length cannot exceed buffer */
    if (len > HMI_RX_DMA_BUF_SIZE)
        len = 0;

    /* ── Copy new bytes into the ISR→thread ring buffer ────── */
    for (uint16_t i = 0; i < len; i++)
    {
        uint16_t idx = (dma_rd_pos + i) % HMI_RX_DMA_BUF_SIZE;
        hmi_rxbuf_push_isr(&rx_ring, rx_dma_buf[idx]);
    }

    dma_rd_pos = wr_pos;

    /* Wake unpack thread */
    if (len > 0 && unpack_thread_id != NULL)
    {
        osThreadFlagsSet(unpack_thread_id, HMI_UART_RX_RDY);
    }
}

/* ================================================================ */
/*  TX — interrupt-driven, non-blocking                              */
/* ================================================================ */

bool hmi_uart_send(const uint8_t *data, uint32_t len)
{
    if (len == 0U)
        return true;
    if (data == NULL || tx_mutex == NULL)
        return false;
    if (osKernelGetState() != osKernelRunning)
        return false;

    if (osMutexAcquire(tx_mutex, HMI_UART_TX_TIMEOUT_MS) != osOK)
        return false;

    bool ok = true;
    uint32_t offset = 0U;
    tx_wait_thread = osThreadGetId();

    if (tx_wait_thread == NULL)
    {
        ok = false;
    }

    while (ok && offset < len)
    {
        uint32_t remain = len - offset;
        uint16_t chunk = (remain > 0xFFFFUL) ? 0xFFFFU : (uint16_t)remain;
        uint32_t flags;

        (void)osThreadFlagsClear(HMI_UART_TX_FLAGS);

        if (HAL_UART_Transmit_DMA(&hmi_huart, (uint8_t *)&data[offset], chunk) != HAL_OK)
        {
            ok = false;
            break;
        }

        flags = osThreadFlagsWait(HMI_UART_TX_FLAGS, osFlagsWaitAny, HMI_UART_TX_TIMEOUT_MS);
        if ((flags & osFlagsError) != 0U || (flags & HMI_UART_TX_ERROR) != 0U)
        {
            (void)HAL_UART_AbortTransmit(&hmi_huart);
            ok = false;
            break;
        }

        offset += chunk;
    }

    tx_wait_thread = NULL;
    osMutexRelease(tx_mutex);
    return ok;
}

/** Called by HAL after each TX DMA transfer completes (weak override) */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != hmi_huart.Instance)
        return;
    if (tx_wait_thread != NULL)
    {
        osThreadFlagsSet(tx_wait_thread, HMI_UART_TX_DONE);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != hmi_huart.Instance)
        return;
    if (tx_wait_thread != NULL)
    {
        osThreadFlagsSet(tx_wait_thread, HMI_UART_TX_ERROR);
    }
}

/* ================================================================ */
/*  RTOS unpack thread                                               */
/* ================================================================ */

/*
void cnt_test_task(void *arg)
{
    while (1)
    {
        cnt_view++;
        if (cnt_view > 100)
        {
            cnt_view = 0;
        }
        osDelay(10);
    }
}
*/

void hmi_uart_os_init(void)
{
    if (evt_mutex == NULL)
    {
        evt_mutex = osMutexNew(&hmi_evt_mutex_attr);
    }
    if (tx_mutex == NULL)
    {
        tx_mutex = osMutexNew(&hmi_tx_mutex_attr);
    }
    if (unpack_thread_id == NULL)
    {
        unpack_thread_id = osThreadNew(hmi_uart_unpack_task, NULL, &hmi_unpack_thread_attr);
    }
    hmi_uart_init();
}

static void hmi_uart_unpack_task(void *arg)
{
    (void)arg;

    while (1)
    {
        uint32_t flags;

        /* For testing: print the current ring buffer content every 10s */
        /* Wait for new RX data (from IDLE callback) */
        flags = osThreadFlagsWait(HMI_UART_RX_RDY, osFlagsWaitAny, osWaitForever);
        if ((flags & osFlagsError) != 0U)
        {
            continue;
        }

        /* Unpack frames from the ring buffer */
        hmi_driver_unpack(&rx_ring);
    }
}
