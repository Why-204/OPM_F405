#ifndef ADC_TASK_H
#define ADC_TASK_H

#include <stdint.h>
#include "ADC_Types.h"

extern ADC_ChannelData adc_ch[ADC_CHANNEL_COUNT];
extern ADC_FrameData adc_latest_frame;
extern ADC_DriverStats adc_driver_stats;
extern uint8_t adc_data_valid;

typedef enum
{
    ADC_TASK_STATE_STOPPED = 0,
    ADC_TASK_STATE_CREATING,
    ADC_TASK_STATE_CREATED,
    ADC_TASK_STATE_CREATE_FAILED,
    ADC_TASK_STATE_STARTING,
    ADC_TASK_STATE_UNPACK_INIT,
    ADC_TASK_STATE_DRIVER_INIT,
    ADC_TASK_STATE_DRIVER_START,
    ADC_TASK_STATE_RUNNING,
    ADC_TASK_STATE_RUNNING_WITH_DATA,
    ADC_TASK_STATE_ERROR
} ADC_TaskState;

extern volatile ADC_TaskState adc_task_state;
extern volatile ADC_Status adc_task_last_status;
extern volatile uint32_t adc_task_init_attempts;
extern volatile uint32_t adc_task_alive_tick;

void adc_task_init(void);
uint8_t adc_task_get_latest(ADC_FrameData *frame);

#endif /* ADC_TASK_H */
