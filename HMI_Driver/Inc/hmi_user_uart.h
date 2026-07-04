/**
 * @file    hmi_user_uart.h
 * @brief   HMI UART abstraction layer.
 *          - IDLE + DMA reception with double-buffer ring
 *          - Interrupt-driven TX from a software TX queue
 *          - Unpack thread that feeds parsed events to HmiEventQueue
 */

#ifndef _HMI_USER_UART_H
#define _HMI_USER_UART_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "cmsis_os.h"
#include "main.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ================================================================ */
/*  Configurable UART instance (change as needed)                    */
/* ================================================================ */
#ifndef hmi_huart
#define hmi_huart huart4
#endif

/* ================================================================ */
/*  Buffer sizes                                                     */
/* ================================================================ */
#define HMI_RX_DMA_BUF_SIZE 128U /* DMA RX buffer per bank        */
#define HMI_EVENT_QUEUE_SIZE 32U /* pending events capacity       */

    /* ================================================================ */
    /*  Event types                                                      */
    /* ================================================================ */
    typedef enum
    {
        HMI_EVENT_NONE = 0,
        HMI_EVENT_SCREEN_CHANGE,  /* screen_id valid                */
        HMI_EVENT_CONTROL_NOTIFY, /* control_id, ctrl_type, value   */
        HMI_EVENT_TOUCH,          /* x, y, press/release            */
        HMI_EVENT_RTC,            /* RTC time received              */
    } HmiEventType;

    /* Control types (matches VisualTFT enum) */
    typedef enum
    {
        HMI_CTRL_UNKNOWN = 0x00,
        HMI_CTRL_BUTTON = 0x10,
        HMI_CTRL_TEXT = 0x11,
        HMI_CTRL_PROGRESS = 0x12,
        HMI_CTRL_SLIDER = 0x13,
        HMI_CTRL_METER = 0x14,
    } HmiCtrlType;

    /* ================================================================ */
    /*  Event data structure                                             */
    /* ================================================================ */
    typedef struct
    {
        HmiEventType type;
        uint16_t screen_id;
        uint16_t control_id;
        uint8_t ctrl_type; /* HmiCtrlType                    */
        uint8_t state;     /* button: 0/1; touch: press/rel  */
        uint32_t value;    /* int32 value                    */
        uint16_t x, y;     /* touch coordinates              */
        uint8_t param[32]; /* extra payload (text, RTC, etc) */
        uint8_t param_len;
    } HmiEvent;

    /* ================================================================ */
    /*  Event queue (simple ring buffer)                                 */
    /* ================================================================ */
    typedef struct
    {
        HmiEvent events[HMI_EVENT_QUEUE_SIZE];
        uint16_t head;
        uint16_t tail;
    } HmiEventQueue;

    /* ================================================================ */
    /*  Public API                                                       */
    /* ================================================================ */

    /* ---- UART lifecycle --------------------------------------------- */

    /**
     * Initialize HMI UART: start IDLE+DMA reception on HMI_UART_HANDLE.
     * Must be called after CubeMX HAL_UART_MspInit / MX_USARTx_UART_Init.
     */
    void hmi_uart_init(void);

    /**
     * Start the RTOS unpack thread.
     * Must be called from a FreeRTOS task context (e.g. MX_FREERTOS_Init).
     */
    void hmi_uart_os_init(void);

    /* ---- Send ------------------------------------------------------- */

    /**
     * Send arbitrary bytes over the HMI UART using DMA.
     * The call waits until the DMA transfer-complete callback signals the
     * calling thread. Calls are serialized internally.
     * @return true if all bytes were sent, false on DMA start/error/timeout.
     */
    bool hmi_uart_send(const uint8_t *data, uint32_t len);

    /**
     * Send bytes over the HMI UART (blocking).  DEPRECATED — use
     * hmi_uart_send() instead.  Kept for reference only.
     * @return true if transmit succeeded, false on HAL error.
     */
    // bool hmi_uart_send_blocking(const uint8_t *data, uint16_t len, uint32_t timeout);

    /* ---- Receive (ISR entry, called from stm32f4xx_it.c) ------------ */

    /**
     * Must be called from USART2_IRQHandler after HAL_UART_IRQHandler()
     * when the UART IDLE flag is set.  Copies DMA-received data into the
     * RX ring buffer and wakes the unpack thread.
     */
    void hmi_uart_idle_isr(UART_HandleTypeDef *huart);

    /* ---- Event consumer API (thread-safe) --------------------------- */

    /** Check if there are pending events. Returns count. */
    uint16_t hmi_event_available(void);

    /**
     * Pop one event from the queue (non-blocking).
     * @return true if an event was popped.
     */
    bool hmi_event_pop(HmiEvent *evt);

    /** Push an event into the queue (called by unpack thread). */
    bool hmi_event_push(const HmiEvent *evt);

#ifdef __cplusplus
}
#endif

#endif /* _HMI_USER_UART_H */
