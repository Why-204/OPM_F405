/**
 * @file    cmd_queue.c
 * @brief   HMI RX ring buffer implementation.
 *          ISR-safe head advance, thread-side tail advance.
 */

#include "cmd_queue.h"

/* ================================================================ */
/*  Internal helpers                                                 */
/* ================================================================ */

static inline uint16_t _next(uint16_t idx)
{
    return (idx + 1U) % HMI_RX_BUF_SIZE;
}

/* ================================================================ */
/*  Public API                                                       */
/* ================================================================ */

void hmi_rxbuf_init(HmiRxRingBuf *rb)
{
    rb->head = 0U;
    rb->tail = 0U;
}

void hmi_rxbuf_reset(HmiRxRingBuf *rb)
{
    /* Thread-side reset: zero both pointers (not ISR-safe) */
    rb->head = 0U;
    rb->tail = 0U;
}

bool hmi_rxbuf_push_isr(HmiRxRingBuf *rb, uint8_t byte)
{
    uint16_t next = _next(rb->head);
    if (next == rb->tail)
    {
        return false; /* full — drop byte              */
    }
    rb->buffer[rb->head] = byte;
    rb->head = next;
    return true;
}

bool hmi_rxbuf_pop(HmiRxRingBuf *rb, uint8_t *byte)
{
    if (rb->tail == rb->head)
    {
        return false; /* empty                         */
    }
    *byte = rb->buffer[rb->tail];
    rb->tail = _next(rb->tail);
    return true;
}

uint16_t hmi_rxbuf_available(const HmiRxRingBuf *rb)
{
    if (rb->head >= rb->tail)
    {
        return (uint16_t)(rb->head - rb->tail);
    }
    return (uint16_t)(HMI_RX_BUF_SIZE - rb->tail + rb->head);
}
