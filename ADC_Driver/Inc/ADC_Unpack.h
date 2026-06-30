#ifndef ADC_UNPACK_H
#define ADC_UNPACK_H

#include <stdint.h>
#include "ADC_Types.h"

ADC_Status ADC_Unpack_Init(void);
uint8_t ADC_Unpack_GetLatestFrame(ADC_FrameData *frame);
uint32_t ADC_Unpack_GetFrameCount(void);
void ADC_Driver_FrameReadyCallbackFromISR(void);

#endif /* ADC_UNPACK_H */
