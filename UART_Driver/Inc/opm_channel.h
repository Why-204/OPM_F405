/**
 * @file    opm_channel.h
 * @brief   通用串口通道抽象（一个 UART = 一个 OpmChannel）。
 *
 *  移植自 HMI 的成熟接收机制：
 *    - RX：循环(CIRCULAR) DMA 连续接收 + 手动 IDLE 中断 → 字节环形缓冲 → 唤醒任务
 *    - TX：HAL_UART_Transmit 阻塞发送（mutex 保护），不依赖全局 HAL 回调，
 *          从而避免与 HMI 的 HAL_UART_TxCpltCallback/ErrorCallback 重复定义。
 *    - 帧提取：字节流 → 按协议(0xAA/包长度/校验和)组装成完整帧 → 交 opm_protocol 分发
 *
 *  UART3(CH9121 透传网口) 与 UART5(TTL 转 232) 各实例化一个，互不干扰。
 */

#ifndef _OPM_CHANNEL_H
#define _OPM_CHANNEL_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "cmsis_os.h"
#include "opm_protocol.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ================================================================ */
    /*  缓冲尺寸                                                         */
    /* ================================================================ */
#define OPM_CH_DMA_BUF_SIZE 256U /* 每通道 RX DMA 缓冲（循环）        */
#define OPM_CH_RING_SIZE 512U    /* ISR→任务 字节环形缓冲            */

    /* 任务事件标志：有新 RX 数据可解析 */
#define OPM_CH_RX_RDY 0x0001U

    /* ================================================================ */
    /*  通道实例                                                         */
    /* ================================================================ */
    typedef struct
    {
        UART_HandleTypeDef *huart; /* 绑定的 UART                     */
        const char *name;          /* 调试名，如 "UART5"/"UART3"      */

        /* ---- RX 循环 DMA ---- */
        uint8_t dma_buf[OPM_CH_DMA_BUF_SIZE];
        uint16_t dma_rd_pos; /* 上次已消费到的 DMA 写位置          */

        /* ---- RX 字节环形缓冲（单生产者 ISR / 单消费者 任务）---- */
        uint8_t ring[OPM_CH_RING_SIZE];
        volatile uint16_t ring_head; /* ISR 写                       */
        volatile uint16_t ring_tail; /* 任务读                       */

        /* ---- 帧组装状态机 ---- */
        uint8_t frame_buf[OPM_MAX_FRAME_LEN];
        uint16_t frame_len;      /* 已累积字节数                    */
        uint16_t frame_expected; /* 已知的整帧长度(0=未知)          */

        /* ---- 任务通知 / TX 保护 ---- */
        osThreadId_t task_id; /* 服务本通道的任务，ISR 置位其标志   */
        osMutexId_t tx_mutex; /* 保护 TX                          */

        /* ---- 统计 ---- */
        uint32_t rx_bytes;
        uint32_t frames_ok;
        uint32_t frames_err;
        uint32_t tx_frames;
        uint32_t ring_overflow;
    } OpmChannel;

    /* ================================================================ */
    /*  API                                                             */
    /* ================================================================ */

    /**
     * @brief 初始化通道（清零状态、绑定 UART、创建 TX mutex）。
     *        不启动 DMA；启动请调用 opm_channel_start()。
     */
    void opm_channel_init(OpmChannel *ch, UART_HandleTypeDef *huart, const char *name);

    /**
     * @brief 绑定服务本通道的任务句柄（ISR 收到数据后向其发 OPM_CH_RX_RDY 标志）。
     */
    void opm_channel_bind_task(OpmChannel *ch, osThreadId_t task_id);

    /**
     * @brief 启动接收：将 RX DMA 改为循环模式、使能 IDLE 中断、开始 DMA 接收，
     *        并关闭 DMA TC/HT 中断（循环模式下避免 HAL 回调误关 DMAR）。
     */
    void opm_channel_start(OpmChannel *ch);

    /**
     * @brief IDLE 中断入口，在对应 UARTx_IRQHandler 的 USER CODE 区调用。
     *        计算 DMA 新写入的数据搬入环形缓冲，并唤醒任务。
     */
    void opm_channel_idle_isr(OpmChannel *ch);

    /**
     * @brief 从环形缓冲提取一整帧（非阻塞，状态机可跨多次调用累积）。
     * @param out  解析成功时填充（out->data 指向 ch->frame_buf 内部）
     * @return true=提取到一整帧有效帧；false=数据不足/暂无完整帧。
     * @note  返回的帧在下次调用本函数前有效（frame_buf 会被复用）。
     */
    bool opm_channel_read_frame(OpmChannel *ch, OpmFrame *out);

    /**
     * @brief 阻塞发送一帧（mutex 保护）。
     * @return true=发送成功。
     */
    bool opm_channel_send(OpmChannel *ch, const uint8_t *data, uint16_t len);

    /* ---- 环形缓冲基础操作 ---- */
    bool opm_ring_push_isr(OpmChannel *ch, uint8_t b); /* ISR 侧写入 */
    bool opm_ring_pop(OpmChannel *ch, uint8_t *b);     /* 任务侧读取 */
    uint16_t opm_ring_available(OpmChannel *ch);

#ifdef __cplusplus
}
#endif

#endif /* _OPM_CHANNEL_H */
