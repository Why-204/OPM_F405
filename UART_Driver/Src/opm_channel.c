/**
 * @file    opm_channel.c
 * @brief   通用串口通道实现，见 opm_channel.h。
 *
 *  RX 路径： [CIRCULAR DMA] → [IDLE ISR 搬运] → [字节环形缓冲] → [任务组帧]
 *  TX 路径： opm_channel_send() 阻塞发送（不依赖 HAL 全局回调）
 */

#include "opm_channel.h"
#include <string.h>

/* HAL_UART_Transmit 超时（ms）——最长一帧 ~300B @115200 约 26ms，留足余量 */
#define OPM_TX_TIMEOUT_MS 200U

/* ================================================================ */
/*  环形缓冲（单生产者 ISR / 单消费者 任务）                          */
/* ================================================================ */
bool opm_ring_push_isr(OpmChannel *ch, uint8_t b)
{
    uint16_t next = (uint16_t)((ch->ring_head + 1U) % OPM_CH_RING_SIZE);
    if (next == ch->ring_tail)
    {
        ch->ring_overflow++;
        return false; /* 满，丢弃 */
    }
    ch->ring[ch->ring_head] = b;
    ch->ring_head = next;
    return true;
}

bool opm_ring_pop(OpmChannel *ch, uint8_t *b)
{
    if (ch->ring_tail == ch->ring_head)
    {
        return false; /* 空 */
    }
    *b = ch->ring[ch->ring_tail];
    ch->ring_tail = (uint16_t)((ch->ring_tail + 1U) % OPM_CH_RING_SIZE);
    return true;
}

uint16_t opm_ring_available(OpmChannel *ch)
{
    uint16_t head = ch->ring_head;
    uint16_t tail = ch->ring_tail;
    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }
    return (uint16_t)(OPM_CH_RING_SIZE - tail + head);
}

/* ================================================================ */
/*  初始化 / 启动                                                    */
/* ================================================================ */
void opm_channel_init(OpmChannel *ch, UART_HandleTypeDef *huart, const char *name)
{
    memset(ch, 0, sizeof(*ch));
    ch->huart = huart;
    ch->name = name;

    static const osMutexAttr_t tx_mutex_attr = {.name = "opm_tx"};
    if (ch->tx_mutex == NULL)
    {
        ch->tx_mutex = osMutexNew(&tx_mutex_attr);
    }
}

void opm_channel_bind_task(OpmChannel *ch, osThreadId_t task_id)
{
    ch->task_id = task_id;
}

void opm_channel_start(OpmChannel *ch)
{
    ch->dma_rd_pos = 0U;
    ch->ring_head = 0U;
    ch->ring_tail = 0U;
    ch->frame_len = 0U;
    ch->frame_expected = 0U;

    /* CubeMX 配置为 NORMAL，这里改成 CIRCULAR，DMA 永不停，无需重启 */
    ch->huart->hdmarx->Instance->CR |= DMA_SxCR_CIRC;

    /* 启动 IDLE + DMA 接收 */
    __HAL_UART_CLEAR_IDLEFLAG(ch->huart);
    __HAL_UART_ENABLE_IT(ch->huart, UART_IT_IDLE);
    HAL_UART_Receive_DMA(ch->huart, ch->dma_buf, OPM_CH_DMA_BUF_SIZE);

    /* 关闭 DMA TC/HT 中断：循环模式下 HAL 的 RxCplt 回调会清 CR3_DMAR 而杀死接收 */
    __HAL_DMA_DISABLE_IT(ch->huart->hdmarx, DMA_IT_TC | DMA_IT_HT);
}

/* ================================================================ */
/*  IDLE 中断入口（由 UARTx_IRQHandler 调用）                         */
/* ================================================================ */
void opm_channel_idle_isr(OpmChannel *ch)
{
    UART_HandleTypeDef *huart = ch->huart;

    /* 清除可能的错误标志(ORE/FE/NE)，避免噪声导致接收停滞 */
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE))
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
    }

    if (!(__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE)))
    {
        return;
    }
    __HAL_UART_CLEAR_IDLEFLAG(huart);

    /* 循环 DMA：NDTR = 到下次回绕前剩余字节；当前写位置 = SIZE - NDTR */
    uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(huart->hdmarx);
    uint16_t wr_pos = (uint16_t)(OPM_CH_DMA_BUF_SIZE - ndtr);

    uint16_t len;
    if (wr_pos >= ch->dma_rd_pos)
    {
        len = (uint16_t)(wr_pos - ch->dma_rd_pos);
    }
    else
    {
        len = (uint16_t)(OPM_CH_DMA_BUF_SIZE - ch->dma_rd_pos + wr_pos);
    }
    if (len > OPM_CH_DMA_BUF_SIZE)
    {
        len = 0U;
    }

    for (uint16_t i = 0U; i < len; i++)
    {
        uint16_t idx = (uint16_t)((ch->dma_rd_pos + i) % OPM_CH_DMA_BUF_SIZE);
        (void)opm_ring_push_isr(ch, ch->dma_buf[idx]);
    }
    ch->dma_rd_pos = wr_pos;
    ch->rx_bytes += len;

    if (len > 0U && ch->task_id != NULL)
    {
        (void)osThreadFlagsSet(ch->task_id, OPM_CH_RX_RDY);
    }
}

/* ================================================================ */
/*  帧组装状态机：字节流 → 完整帧                                    */
/* ================================================================ */
bool opm_channel_read_frame(OpmChannel *ch, OpmFrame *out)
{
    uint8_t b;

    while (opm_ring_pop(ch, &b))
    {
        if (ch->frame_len == 0U)
        {
            /* 寻找包头 0xAA */
            if (b == OPM_FRAME_HEADER)
            {
                ch->frame_buf[0] = b;
                ch->frame_len = 1U;
                ch->frame_expected = 0U;
            }
            /* 否则丢弃继续找 */
            continue;
        }

        /* 累积字节 */
        ch->frame_buf[ch->frame_len++] = b;

        /* 收满前 3 字节(包头+包长度)后，确定整帧长度 = 包长度 + 3 */
        if (ch->frame_len == (OPM_HEADER_LEN + OPM_LENGTH_LEN))
        {
            uint16_t length_field = opm_rd_u16le(&ch->frame_buf[OPM_HEADER_LEN]);
            uint16_t total = (uint16_t)(length_field + OPM_HEADER_LEN + OPM_LENGTH_LEN);

            /* 合法性：至少要有命令字+校验和；不能超过缓冲 */
            if (total < OPM_OVERHEAD || total > OPM_MAX_FRAME_LEN)
            {
                /* 非法长度 → 丢弃当前包头，重新同步 */
                ch->frame_len = 0U;
                ch->frame_expected = 0U;
                continue;
            }
            ch->frame_expected = total;
        }

        /* 已知整帧长度且已收齐 → 校验并输出 */
        if (ch->frame_expected != 0U && ch->frame_len >= ch->frame_expected)
        {
            int r = opm_frame_parse(ch->frame_buf, ch->frame_len, out);
            bool ok = (r > 0);

            ch->frame_len = 0U;
            ch->frame_expected = 0U;

            if (ok)
            {
                ch->frames_ok++;
                return true;
            }
            else
            {
                ch->frames_err++;
                /* 校验失败：丢弃，继续从后续字节重新同步 */
                continue;
            }
        }
    }

    return false; /* 环形缓冲已空，暂无完整帧 */
}

/* ================================================================ */
/*  TX — 阻塞发送                                                    */
/* ================================================================ */
bool opm_channel_send(OpmChannel *ch, const uint8_t *data, uint16_t len)
{
    if (len == 0U)
    {
        return true;
    }
    if (data == NULL || ch->tx_mutex == NULL)
    {
        return false;
    }

    if (osMutexAcquire(ch->tx_mutex, OPM_TX_TIMEOUT_MS) != osOK)
    {
        return false;
    }

    HAL_StatusTypeDef st = HAL_UART_Transmit(ch->huart, (uint8_t *)data, len, OPM_TX_TIMEOUT_MS);
    if (st == HAL_OK)
    {
        ch->tx_frames++;
    }

    osMutexRelease(ch->tx_mutex);
    return (st == HAL_OK);
}
