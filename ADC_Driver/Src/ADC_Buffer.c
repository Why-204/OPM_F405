#include "ADC_Buffer.h"
#include "stm32f4xx_hal.h"
#include <string.h>

static ADC_RawFrame s_ring[ADC_RAW_FRAME_RING_DEPTH];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;

static uint16_t adc_buffer_next(uint16_t index)
{
    index++;
    if (index >= ADC_RAW_FRAME_RING_DEPTH)
    {
        index = 0U;
    }
    return index;
}

void ADC_Buffer_Reset(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_head = 0U;
    s_tail = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

ADC_Status ADC_Buffer_PushFromISR(const uint8_t *data, uint32_t sequence, uint32_t timestamp_ms)
{
    uint16_t next;

    if (data == NULL)
    {
        return ADC_STATUS_INVALID_PARAM;
    }

    next = adc_buffer_next(s_head);
    if (next == s_tail)
    {
        return ADC_STATUS_OVERFLOW;
    }

    memcpy(s_ring[s_head].data, data, ADC_FRAME_BYTES);
    s_ring[s_head].sequence = sequence;
    s_ring[s_head].timestamp_ms = timestamp_ms;
    s_head = next;

    return ADC_STATUS_OK;
}

ADC_Status ADC_Buffer_Pop(ADC_RawFrame *frame)
{
    uint32_t primask;

    if (frame == NULL)
    {
        return ADC_STATUS_INVALID_PARAM;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (s_head == s_tail)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return ADC_STATUS_EMPTY;
    }

    memcpy(frame, &s_ring[s_tail], sizeof(ADC_RawFrame));
    s_tail = adc_buffer_next(s_tail);
    if (primask == 0U)
    {
        __enable_irq();
    }

    return ADC_STATUS_OK;
}

uint32_t ADC_Buffer_Count(void)
{
    uint16_t head;
    uint16_t tail;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    head = s_head;
    tail = s_tail;
    if (primask == 0U)
    {
        __enable_irq();
    }

    if (head >= tail)
    {
        return (uint32_t)(head - tail);
    }
    return (uint32_t)(ADC_RAW_FRAME_RING_DEPTH - tail + head);
}
