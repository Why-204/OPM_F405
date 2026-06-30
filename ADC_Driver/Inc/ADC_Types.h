#ifndef ADC_TYPES_H
#define ADC_TYPES_H

#include <stdint.h>
#include "ADC_Config.h"

typedef enum
{
    ADC_STATUS_OK = 0,
    ADC_STATUS_ERROR,
    ADC_STATUS_INVALID_PARAM,
    ADC_STATUS_BUSY,
    ADC_STATUS_NOT_READY,
    ADC_STATUS_EMPTY,
    ADC_STATUS_OVERFLOW
} ADC_Status;

typedef struct
{
    uint8_t data[ADC_FRAME_BYTES];
    uint32_t sequence;
    uint32_t timestamp_ms;
} ADC_RawFrame;

typedef struct
{
    int32_t raw_code;
    float adc_diff_v;
    float voltage_v;
    float current_a;
    uint8_t valid;
} ADC_ChannelData;

typedef struct
{
    ADC_ChannelData channel[ADC_CHANNEL_COUNT];
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint8_t valid;
} ADC_FrameData;

typedef struct
{
    uint32_t drdy_count;
    uint32_t dma_start_count;
    uint32_t dma_complete_count;
    uint32_t frames_pushed;
    uint32_t frames_popped;
    uint32_t ring_overflow_count;
    uint32_t spi_busy_count;
    uint32_t spi_error_count;
    uint32_t spi_tx_dma_missing_count;
    uint32_t missed_drdy_count;
    uint32_t discarded_frames;
    uint32_t last_hal_status;
    uint32_t last_spi_state;
    uint32_t last_spi_error;
    uint32_t last_dma_error;
    uint32_t last_dma_ndtr;
    uint8_t running;
    uint8_t dma_busy;
} ADC_DriverStats;

#endif /* ADC_TYPES_H */
