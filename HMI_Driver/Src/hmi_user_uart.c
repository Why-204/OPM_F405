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

/* ---- RX DMA buffer (CIRCULAR, DMA writes continuously) ---------- */
static uint8_t rx_dma_buf[HMI_RX_DMA_BUF_SIZE];
static uint16_t dma_rd_pos; /* last consumed NDTR-based position */

/* ---- RX ring buffer (cmd_queue, ISR→thread bridge) -------------- */
static HmiRxRingBuf rx_ring;

/* ---- Event queue ------------------------------------------------- */
static HmiEventQueue evt_queue;
static osMutexId evt_mutex; /* protect consumer access to evt_queue */

/* ---- TX software queue ------------------------------------------- */
static uint8_t tx_queue[HMI_TX_BUF_SIZE];
static uint16_t tx_head, tx_tail;
static bool tx_busy;

/* ---- RTOS objects ------------------------------------------------ */
osThreadId unpack_thread_id;
#define HMI_UART_RX_RDY 0x0001U /* event flag: new DMA data ready */

/* ================================================================ */
/*  Forward declarations                                             */
/* ================================================================ */
static void hmi_uart_tx_next_byte(void);
void hmi_uart_unpack_task(void const *arg);

/* ================================================================ */
/*  Event queue helpers (mutex-protected)                            */
/* ================================================================ */

bool hmi_event_push(const HmiEvent *evt)
{
    if (evt_mutex == NULL)
        return false;
    osMutexWait(evt_mutex, osWaitForever);
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
        return false;
    osMutexWait(evt_mutex, osWaitForever);
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
        return 0;
    osMutexWait(evt_mutex, osWaitForever);
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
    memset(tx_queue, 0, sizeof(tx_queue));
    tx_head = tx_tail = 0;
    tx_busy = false;
    dma_rd_pos = 0U;

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
        osSignalSet(unpack_thread_id, HMI_UART_RX_RDY);
    }
}

/* ================================================================ */
/*  TX — interrupt-driven, non-blocking                              */
/* ================================================================ */

bool hmi_uart_send(const uint8_t *data, uint16_t len)
{
    if (len == 0)
        return true;

    /* Enqueue into TX software queue */
    __disable_irq();
    for (uint16_t i = 0; i < len; i++)
    {
        uint16_t next = (tx_head + 1U) % HMI_TX_BUF_SIZE;
        if (next == tx_tail)
        {
            __enable_irq();
            return false; /* TX queue full */
        }
        tx_queue[tx_head] = data[i];
        tx_head = next;
    }
    __enable_irq();

    /* Kick off TX if not already transmitting */
    if (!tx_busy)
    {
        hmi_uart_tx_next_byte();
    }
    return true;
}

/*
bool hmi_uart_send_blocking(const uint8_t *data, uint16_t len, uint32_t timeout)
{
    if (len == 0)
        return true;
    return (HAL_UART_Transmit(&hmi_huart, (uint8_t *)data, len, timeout) == HAL_OK);
}
*/

static void hmi_uart_tx_next_byte(void)
{
    if (tx_tail == tx_head)
    {
        tx_busy = false;
        __HAL_UART_DISABLE_IT(&hmi_huart, UART_IT_TXE);
        return;
    }

    /* Only kick off a new IT transfer when the UART is globally
     * ready.  If a previous IT transfer is still flushing the
     * last byte (TXE not yet serviced) the HAL will return BUSY
     * and TXE ISR will chain to us via TxCpltCallback later. */
    if (hmi_huart.gState != HAL_UART_STATE_READY)
        return;

    tx_busy = true;
    if (HAL_UART_Transmit_IT(&hmi_huart, &tx_queue[tx_tail], 1) != HAL_OK)
    {
        tx_busy = false;
    }
}

/** Called by HAL after each TXE byte completes (weak override) */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != hmi_huart.Instance)
        return;
    tx_tail = (tx_tail + 1U) % HMI_TX_BUF_SIZE;
    hmi_uart_tx_next_byte();
}

/* ================================================================ */
/*  RTOS unpack thread                                               */
/* ================================================================ */

/*
void cnt_test_task(void const *arg)
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
    hmi_uart_init();
    osMutexDef(hmi_evt_mutex);
    evt_mutex = osMutexCreate(osMutex(hmi_evt_mutex));

    osThreadDef(hmi_unpack, hmi_uart_unpack_task, osPriorityNormal, 0, 128);
    unpack_thread_id = osThreadCreate(osThread(hmi_unpack), NULL);
}

void hmi_uart_unpack_task(void const *arg)
{
    while (1)
    {
        /* For testing: print the current ring buffer content every 10s */
        /* Wait for new RX data (from IDLE callback) */
        osSignalWait(HMI_UART_RX_RDY, osWaitForever);

        /* Unpack frames from the ring buffer */
        hmi_driver_unpack(&rx_ring);
    }
}
