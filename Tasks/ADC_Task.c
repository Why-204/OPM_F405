#include "ADC_Task.h"
#include "ADC_Driver.h"
#include "ADC_Unpack.h"
#include "cmsis_os.h"
#include "main.h"
#include <string.h>
#include "math.h"

extern SPI_HandleTypeDef hspi2;

ADC_ChannelData adc_ch[ADC_CHANNEL_COUNT];
ADC_FrameData adc_latest_frame;
ADC_DriverStats adc_driver_stats;
uint8_t adc_data_valid;

Analog_Calibration_Config ana_cali_cfg;

volatile ADC_TaskState adc_task_state = ADC_TASK_STATE_STOPPED;
volatile ADC_Status adc_task_last_status = ADC_STATUS_ERROR;
volatile uint32_t adc_task_init_attempts;
volatile uint32_t adc_task_alive_tick;
volatile uint32_t adc_task_store_cnt;
volatile uint32_t adc_task_last_sequence;

osThreadId_t s_adc_task_thread;

static void adc_task_thread(void *argument);

static const osThreadAttr_t s_adc_task_attr = {
    .name = "adc_task",
    .stack_size = ADC_TASK_THREAD_STACK_BYTES,
    .priority = (osPriority_t)osPriorityNormal,
};

void update_cali_cfg(float Vref, float Current_k, float Current_b)
{
    ana_cali_cfg.Vref = Vref;
    ana_cali_cfg.V_per_code = Vref / ADC_CODE_FULL_SCALE;
    ana_cali_cfg.K = 1 / Current_k;
    ana_cali_cfg.B = Current_b;
}

void adc_task_init(void)
{
    adc_task_init_attempts++;
    update_cali_cfg(2.5013f, 0.3928f, 5.2619f);
    if (s_adc_task_thread == NULL)
    {
        adc_task_state = ADC_TASK_STATE_CREATING;
        s_adc_task_thread = osThreadNew(adc_task_thread, NULL, &s_adc_task_attr);
        adc_task_state = (s_adc_task_thread != NULL) ? ADC_TASK_STATE_CREATED : ADC_TASK_STATE_CREATE_FAILED;
    }
}

static void adc_task_thread(void *argument)
{
    ADC_Status status;
    uint32_t i, j;
    (void)argument;

    adc_task_state = ADC_TASK_STATE_STARTING;

    do
    {
        adc_task_alive_tick++;
        adc_task_state = ADC_TASK_STATE_UNPACK_INIT;
        status = ADC_Driver_Init(&hspi2);
        if (status == ADC_STATUS_OK)
        {
            adc_task_state = ADC_TASK_STATE_DRIVER_INIT;
            status = ADC_Unpack_Init();
        }
        if (status == ADC_STATUS_OK)
        {
            adc_task_state = ADC_TASK_STATE_DRIVER_START;
            status = ADC_Driver_Start();
        }
        adc_task_last_status = status;
        if (status != ADC_STATUS_OK)
        {
            adc_task_state = ADC_TASK_STATE_ERROR;
            ADC_Driver_GetStats(&adc_driver_stats);
            osDelay(1000U);
        }
    } while (status != ADC_STATUS_OK);

    adc_task_state = ADC_TASK_STATE_RUNNING;

    for (;;)
    {
        osThreadFlagsWait(ADC_AVG_READY, osFlagsWaitAny, 10);
        ADC_Driver_GetStats(&adc_driver_stats);
        for (i = 0; i < ADC_CHANNEL_COUNT; i++)
        {
            adc_ch[i].raw_code = adc_frameAvgData.channel[i];
            adc_ch[i].adc_diff_v = adc_ch[i].raw_code * ana_cali_cfg.V_per_code;
            adc_ch[i].voltage_v = ana_cali_cfg.Vref - adc_ch[i].adc_diff_v;
            adc_ch[i].log_current = (adc_ch[i].voltage_v - ana_cali_cfg.B) * ana_cali_cfg.K;
            adc_ch[i].current_a = powf(10, adc_ch[i].log_current);
        }
    }
}
