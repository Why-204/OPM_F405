#ifndef ADC_UNPACK_H
#define ADC_UNPACK_H

#include <stdint.h>
#include "ADC_Types.h"
#include "cmsis_os.h"

#define ADC_UNPACK_START 1u
#define ADC_AVG_START 2u
#define ADC_AVG_RESET 4u
#define ADC_AVG_READY 1u

extern uint32_t adc_unpack_cnt;
extern osThreadId_t s_unpack_thread;

extern ADC_FrameData adc_frameAvgData;
extern ADC_FrameData adc_frame4ms;      /* Stage-1：最近一个 4ms 窗口均值(原始码) */
extern uint32_t adc_current_frame_index;
extern uint32_t adc_acc_frame_length;

ADC_Status ADC_Unpack_Init(void);
ADC_Status ADC_Unpack_SetAverageTimeUs(uint32_t sample_us);
#endif /* ADC_UNPACK_H */
