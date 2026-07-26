#include "ADC_Unpack.h"
#include "ADC_Driver.h"
#include "cmsis_os.h"
#include <math.h>
#include <string.h>

osThreadId_t s_unpack_thread;
extern osThreadId_t s_adc_task_thread;

static uint32_t adc_unpack_cnt;
int32_t adc_unpack_acc_raw_code[ADC_CHANNEL_COUNT];
int32_t adc_unpack_acc_length;
int32_t adc_unpack_valid_length;

ADC_FrameData adc_frameData[ADC_MAX_FRAME_LENGTH];
ADC_FrameData adc_frameSumData;
ADC_FrameData adc_frameAvgData;
uint32_t adc_current_frame_index = 0;
uint32_t adc_acc_frame_length = ADC_MAX_FRAME_LENGTH;

extern TIM_HandleTypeDef htim14;
static void adc_unpack_thread(void *argument);

/* TIM14 目前采用 72 分频，计数时钟约 750 kHz；16 位自动重装上限约 87.38 ms。 */
#define ADC_AVG_TIMER_MAX_US 87381U

static const osThreadAttr_t s_unpack_thread_attr = {
    .name = "adc_unpack",
    .stack_size = ADC_UNPACK_THREAD_STACK_BYTES,
    .priority = (osPriority_t)osPriorityAboveNormal,
};

static int32_t adc_unpack_s24(const uint8_t *data)
{
    uint32_t value;

    value = ((int32_t)((int8_t)data[0]) << 16) | ((uint32_t)data[1] << 8) | (uint32_t)data[2];

    return (int32_t)value;
}

ADC_Status ADC_Unpack_Init(void)
{
    if (s_unpack_thread != NULL)
    {
        return ADC_STATUS_OK;
    }
    adc_unpack_cnt = 0U;
    adc_unpack_valid_length = 0U;
    memset((void *)adc_unpack_acc_raw_code, 0, sizeof(adc_unpack_acc_raw_code));
    memset((void *)&adc_frameAvgData, 0, sizeof(adc_frameAvgData));
    memset((void *)&adc_frameSumData, 0, sizeof(adc_frameSumData));
    s_unpack_thread = osThreadNew(adc_unpack_thread, NULL, &s_unpack_thread_attr);
    if (s_unpack_thread == NULL)
    {
        return ADC_STATUS_ERROR;
    }

    return ADC_STATUS_OK;
}

static void adc_unpack_thread(void *argument)
{
    uint32_t i = 0, j = 0;
    uint32_t flags = 0;
    (void)argument;
    HAL_TIM_Base_Start_IT(&htim14);

    for (;;)
    {
        flags = osThreadFlagsWait(ADC_UNPACK_START | ADC_AVG_START | ADC_AVG_RESET, osFlagsWaitAny, 10);
        if (flags == osFlagsError)
        {
            osDelay(10);
            continue;
        }
        if (flags & ADC_AVG_RESET)
        {
            memset(adc_frameData, 0, sizeof(adc_frameData));
            memset(&adc_frameAvgData, 0, sizeof(adc_frameAvgData));
            memset(&adc_frameSumData, 0, sizeof(adc_frameSumData));
            adc_unpack_acc_length = 0;
            adc_unpack_valid_length = 0;
            adc_current_frame_index = 0;
            continue;
        }
        if (flags & ADC_UNPACK_START)
        {
            for (i = 0, j = 0; i < ADC_CHANNEL_COUNT; i++, j += 3)
            {
                adc_unpack_acc_raw_code[i] += adc_unpack_s24((uint8_t *)adc_latest_raw_bytes + j);
            }
            adc_unpack_acc_length += 1;
        }
        if (flags & ADC_AVG_START)
        {
            adc_unpack_valid_length = adc_unpack_valid_length + (adc_unpack_valid_length < adc_acc_frame_length);
            for (i = 0; i < ADC_CHANNEL_COUNT; i++)
            {
                int32_t new_value = adc_unpack_acc_raw_code[i] / adc_unpack_acc_length;
                adc_frameSumData.channel[i] = adc_frameSumData.channel[i] - adc_frameData[adc_current_frame_index].channel[i] + new_value;
                adc_frameData[adc_current_frame_index].channel[i] = new_value;
                adc_unpack_acc_raw_code[i] = 0;
                adc_frameAvgData.channel[i] = adc_frameSumData.channel[i] / adc_unpack_valid_length;
            }
            adc_frameAvgData.valid = 1;
            adc_frameData[adc_current_frame_index].valid = 1;

            adc_unpack_acc_length = 0;
            adc_current_frame_index = (adc_current_frame_index + 1) % adc_acc_frame_length;
            adc_frameData[adc_current_frame_index].valid = 0;

            osThreadFlagsSet(s_adc_task_thread, ADC_AVG_READY);
        }
    }
}

ADC_Status adc_set_frame_length(uint32_t frame_length)
{
    if (frame_length < ADC_MAX_FRAME_LENGTH)
    {
        adc_acc_frame_length = frame_length;
        return ADC_STATUS_OK;
    }
    return ADC_STATUS_INVALID_PARAM;
}

ADC_Status ADC_Unpack_SetAverageTimeUs(uint32_t sample_us)
{
    uint32_t timer_us;
    uint32_t frame_length;
    uint32_t timer_ticks;

    if (sample_us < 50U)
    {
        return ADC_STATUS_INVALID_PARAM;
    }

    /* 现有 TIM14 只能在约 87ms 以内按 us 级重装；更长时间用滑动窗补足。 */
    if (sample_us <= ADC_AVG_TIMER_MAX_US)
    {
        timer_us = sample_us;
        frame_length = 1U;
    }
    else
    {
        timer_us = ADC_AVG_TIMER_MAX_US;
        frame_length = (sample_us + timer_us - 1U) / timer_us;
        if (frame_length >= ADC_MAX_FRAME_LENGTH)
        {
            frame_length = ADC_MAX_FRAME_LENGTH - 1U;
        }
    }

    /* 750 kHz 计数时钟：1 tick ≈ 1.333us。换算为 ARR = ticks - 1。 */
    timer_ticks = ((timer_us * 3U) + 2U) / 4U;
    if (timer_ticks == 0U)
    {
        timer_ticks = 1U;
    }
    if (timer_ticks > 0x10000U)
    {
        timer_ticks = 0x10000U;
    }

    if (adc_set_frame_length(frame_length) != ADC_STATUS_OK)
    {
        return ADC_STATUS_INVALID_PARAM;
    }

    if (HAL_TIM_Base_Stop_IT(&htim14) != HAL_OK)
    {
        return ADC_STATUS_ERROR;
    }

    __HAL_TIM_SET_AUTORELOAD(&htim14, (uint32_t)(timer_ticks - 1U));
    __HAL_TIM_SET_COUNTER(&htim14, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim14, TIM_FLAG_UPDATE);

    if (HAL_TIM_Base_Start_IT(&htim14) != HAL_OK)
    {
        return ADC_STATUS_ERROR;
    }

    adc_unpack_acc_length = 0;
    adc_unpack_valid_length = 0;
    adc_current_frame_index = 0;
    memset(adc_frameData, 0, sizeof(adc_frameData));
    memset(&adc_frameAvgData, 0, sizeof(adc_frameAvgData));
    memset(&adc_frameSumData, 0, sizeof(adc_frameSumData));

    if (s_unpack_thread != NULL)
    {
        osThreadFlagsSet(s_unpack_thread, ADC_AVG_RESET);
    }

    return ADC_STATUS_OK;
}
