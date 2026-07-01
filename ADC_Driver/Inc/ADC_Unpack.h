#ifndef ADC_UNPACK_H
#define ADC_UNPACK_H

#include <stdint.h>
#include "ADC_Types.h"

extern volatile uint32_t adc_unpack_cnt;
extern volatile uint32_t adc_unpack_nonzero_channel_mask;
extern volatile uint8_t adc_unpack_last_raw_bytes[ADC_FRAME_BYTES];
extern volatile int32_t adc_unpack_last_raw_code[ADC_CHANNEL_COUNT];
extern volatile float adc_unpack_last_adc_diff_v[ADC_CHANNEL_COUNT];
extern volatile float adc_unpack_last_voltage_v[ADC_CHANNEL_COUNT];

ADC_Status ADC_Unpack_Init(void);
uint8_t ADC_Unpack_GetLatestFrame(ADC_FrameData *frame);
uint32_t ADC_Unpack_GetFrameCount(void);
void ADC_Driver_FrameReadyCallbackFromISR(void);

#endif /* ADC_UNPACK_H */
