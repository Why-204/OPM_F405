#include "ADC_Task.h"
#include "ADC_Driver.h"
#include "ADC_Unpack.h"
#include "cmsis_os.h"
#include "main.h"
#include <string.h>

extern SPI_HandleTypeDef hspi2;

ADC_ChannelData adc_ch[ADC_CHANNEL_COUNT];
ADC_FrameData adc_latest_frame;
ADC_DriverStats adc_driver_stats;
uint8_t adc_data_valid;
volatile ADC_TaskState adc_task_state = ADC_TASK_STATE_STOPPED;
volatile ADC_Status adc_task_last_status = ADC_STATUS_ERROR;
volatile uint32_t adc_task_init_attempts;
volatile uint32_t adc_task_alive_tick;

static osThreadId_t s_adc_task_thread;

static void adc_task_thread(void *argument);

static const osThreadAttr_t s_adc_task_attr = {
    .name = "adc_task",
    .stack_size = ADC_TASK_THREAD_STACK_BYTES,
    .priority = (osPriority_t)osPriorityNormal,
};

void adc_task_init(void)
{
    adc_task_init_attempts++;

    if (s_adc_task_thread == NULL)
    {
        adc_task_state = ADC_TASK_STATE_CREATING;
        s_adc_task_thread = osThreadNew(adc_task_thread, NULL, &s_adc_task_attr);
        adc_task_state = (s_adc_task_thread != NULL) ? ADC_TASK_STATE_CREATED : ADC_TASK_STATE_CREATE_FAILED;
    }
}

uint8_t adc_task_get_latest(ADC_FrameData *frame)
{
    uint32_t primask;

    if ((frame == NULL) || (adc_data_valid == 0U))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    memcpy(frame, &adc_latest_frame, sizeof(adc_latest_frame));
    if (primask == 0U)
    {
        __enable_irq();
    }

    return frame->valid;
}

static void adc_task_store_frame(const ADC_FrameData *frame)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    memcpy(&adc_latest_frame, frame, sizeof(adc_latest_frame));
    memcpy(adc_ch, frame->channel, sizeof(adc_ch));
    adc_data_valid = frame->valid;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static void adc_task_thread(void *argument)
{
    ADC_FrameData frame;
    ADC_Status status;

    (void)argument;

    adc_task_state = ADC_TASK_STATE_STARTING;

    do
    {
        adc_task_alive_tick++;
        adc_task_state = ADC_TASK_STATE_UNPACK_INIT;
        status = ADC_Unpack_Init();
        if (status == ADC_STATUS_OK)
        {
            adc_task_state = ADC_TASK_STATE_DRIVER_INIT;
            status = ADC_Driver_Init(&hspi2);
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
        adc_task_alive_tick++;

        if (ADC_Unpack_GetLatestFrame(&frame) != 0U)
        {
            adc_task_store_frame(&frame);
            adc_task_state = ADC_TASK_STATE_RUNNING_WITH_DATA;
        }
        else
        {
            adc_task_state = ADC_TASK_STATE_RUNNING;
        }

        ADC_Driver_GetStats(&adc_driver_stats);
        osDelay(ADC_TASK_PERIOD_MS);
    }
}
