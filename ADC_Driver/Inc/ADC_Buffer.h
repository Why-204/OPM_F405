#ifndef ADC_BUFFER_H
#define ADC_BUFFER_H

#include <stdint.h>
#include "ADC_Types.h"

void ADC_Buffer_Reset(void);
ADC_Status ADC_Buffer_PushFromISR(const uint8_t *data, uint32_t sequence, uint32_t timestamp_ms);
ADC_Status ADC_Buffer_Pop(ADC_RawFrame *frame);
uint32_t ADC_Buffer_Count(void);

#endif /* ADC_BUFFER_H */
