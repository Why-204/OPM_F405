/**
 * @file    cmd_queue.h
 * @brief   HMI RX ring buffer — pure byte-level circular buffer,
 *          used by hmi_user_uart ISR to store received bytes before unpacking.
 */

#ifndef _CMD_QUEUE_H
#define _CMD_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ------------------------------------------------------------------ */
/*  Configurable constants                                             */
/* ------------------------------------------------------------------ */
#ifndef HMI_RX_BUF_SIZE
#define HMI_RX_BUF_SIZE 128U /* RX ring buffer capacity (bytes) */
#endif

    /* ------------------------------------------------------------------ */
    /*  Ring buffer structure                                              */
    /* ------------------------------------------------------------------ */
    typedef struct
    {
        uint8_t buffer[HMI_RX_BUF_SIZE];
        uint16_t head; /* write index (ISR side)          */
        uint16_t tail; /* read index  (thread side)       */
    } HmiRxRingBuf;

    /* ------------------------------------------------------------------ */
    /*  Public API                                                         */
    /* ------------------------------------------------------------------ */

    /** Reset the ring buffer to empty state */
    void hmi_rxbuf_init(HmiRxRingBuf *rb);

    /**
     * Emergency reset — discard all buffered data.
     * Safe to call from thread context; NOT ISR-safe (accesses both head and tail).
     */
    void hmi_rxbuf_reset(HmiRxRingBuf *rb);

    /**
     * Push one byte (callable from ISR).
     * @return true if byte was accepted, false if buffer full (dropped).
     */
    bool hmi_rxbuf_push_isr(HmiRxRingBuf *rb, uint8_t byte);

    /**
     * Pop one byte (callable from thread context).
     * @return true if a byte was popped, false if empty.
     */
    bool hmi_rxbuf_pop(HmiRxRingBuf *rb, uint8_t *byte);

    /** Return number of bytes currently available for reading */
    uint16_t hmi_rxbuf_available(const HmiRxRingBuf *rb);

#ifdef __cplusplus
}
#endif

#endif /* _CMD_QUEUE_H */
