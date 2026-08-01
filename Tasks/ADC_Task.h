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
extern volatile uint32_t adc_task_store_cnt;
extern volatile uint32_t adc_task_last_sequence;

void adc_task_init(void);
uint8_t adc_task_get_latest(ADC_FrameData *frame);

/* ===== 连续测量(高速版本) 采样引擎 =====
 * 粒度固定 100ms(=25×4ms)。捕获缓冲存 dBmA(float)，
 * RDMR 在协议任务读取后按当前波长偏置换算为 dBm 回传。 */
void adc_capture_start(uint32_t count);                                       /* 启动，采集 count 点后自动停 */
void adc_capture_stop(void);                                                  /* 停止(保留已采数据) */
uint32_t adc_capture_get_done(void);                                          /* 已完成点数 */
uint32_t adc_capture_read(uint8_t ch_index, uint32_t start, float *out, uint32_t n); /* 取历史，返回实际拷贝点数 */

#endif /* ADC_TASK_H */
