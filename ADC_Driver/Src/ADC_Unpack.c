#include "ADC_Unpack.h"
#include "ADC_Driver.h"
#include "cmsis_os.h"
#include <math.h>
#include <string.h>

static osThreadId_t s_unpack_thread;
static osSemaphoreId_t s_frame_sem;
static ADC_FrameData s_latest_frame;
static volatile uint8_t s_latest_valid;
static volatile uint32_t s_unpack_count;

volatile uint32_t adc_unpack_cnt;
volatile uint32_t adc_unpack_nonzero_channel_mask;
volatile uint8_t adc_unpack_last_raw_bytes[ADC_FRAME_BYTES];
volatile int32_t adc_unpack_last_raw_code[ADC_CHANNEL_COUNT];
volatile float adc_unpack_last_adc_diff_v[ADC_CHANNEL_COUNT];
volatile float adc_unpack_last_voltage_v[ADC_CHANNEL_COUNT];

static void adc_unpack_thread(void *argument);

static const osThreadAttr_t s_unpack_thread_attr = {
    .name = "adc_unpack",
    .stack_size = ADC_UNPACK_THREAD_STACK_BYTES,
    .priority = (osPriority_t)osPriorityNormal,
};

static int32_t adc_unpack_s24(const uint8_t *data)
{
    int32_t value;

    value = ((int32_t)data[0] << 16) | ((int32_t)data[1] << 8) | (int32_t)data[2];
    if ((value & 0x00800000L) != 0)
    {
        value |= (int32_t)0xFF000000L;
    }
    return value;
}

static float adc_unpack_current_from_voltage(float voltage_v)
{
#if (ADC_ENABLE_LOG_CURRENT_CONVERSION != 0U)
    float decades = (voltage_v - ADC_LOG_REF_VOLTAGE_V) / ADC_LOG_SLOPE_V_PER_DEC;

    if (decades > 30.0f)
    {
        decades = 30.0f;
    }
    else if (decades < -30.0f)
    {
        decades = -30.0f;
    }

    return ADC_LOG_REF_CURRENT_A * powf(10.0f, decades);
#else
    (void)voltage_v;
    return 0.0f;
#endif
}

static float adc_unpack_voltage_from_adc_diff(float adc_diff_v)
{
    return ADC_FRONTEND_INPUT_AT_ZERO_DIFF_V +
           ((adc_diff_v - ADC_FRONTEND_ADC_DIFF_ZERO_V) * ADC_FRONTEND_INPUT_V_PER_ADC_DIFF_V);
}

static void adc_unpack_convert(const ADC_RawFrame *raw, ADC_FrameData *frame)
{
    uint32_t ch;
    uint32_t nonzero_mask = 0U;

    memset(frame, 0, sizeof(*frame));
    frame->sequence = raw->sequence;
    frame->timestamp_ms = raw->timestamp_ms;
    frame->valid = 1U;
    memcpy((void *)adc_unpack_last_raw_bytes, raw->data, sizeof(adc_unpack_last_raw_bytes));

    for (ch = 0U; ch < ADC_CHANNEL_COUNT; ch++)
    {
        const uint8_t *src = &raw->data[ch * ADC_BYTES_PER_CHANNEL];
        int32_t code = adc_unpack_s24(src);
        float adc_diff_v = ((float)code * ADC_VREF_V) / ADC_CODE_FULL_SCALE;
        float voltage_v = adc_unpack_voltage_from_adc_diff(adc_diff_v);

        frame->channel[ch].raw_code = code;
        frame->channel[ch].adc_diff_v = adc_diff_v;
        frame->channel[ch].voltage_v = voltage_v;
        frame->channel[ch].current_a = adc_unpack_current_from_voltage(voltage_v);
        frame->channel[ch].valid = 1U;

        if ((src[0] != 0U) || (src[1] != 0U) || (src[2] != 0U))
        {
            nonzero_mask |= (1UL << ch);
        }

        adc_unpack_last_raw_code[ch] = code;
        adc_unpack_last_adc_diff_v[ch] = adc_diff_v;
        adc_unpack_last_voltage_v[ch] = voltage_v;
    }
    adc_unpack_nonzero_channel_mask = nonzero_mask;
}

static void adc_unpack_store_latest(const ADC_FrameData *frame)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    memcpy(&s_latest_frame, frame, sizeof(s_latest_frame));
    s_latest_valid = 1U;
    s_unpack_count++;
    adc_unpack_cnt = s_unpack_count;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

ADC_Status ADC_Unpack_Init(void)
{
    if (s_unpack_thread != NULL)
    {
        return ADC_STATUS_OK;
    }

    memset(&s_latest_frame, 0, sizeof(s_latest_frame));
    s_latest_valid = 0U;
    s_unpack_count = 0U;
    adc_unpack_cnt = 0U;
    adc_unpack_nonzero_channel_mask = 0U;
    memset((void *)adc_unpack_last_raw_bytes, 0, sizeof(adc_unpack_last_raw_bytes));
    memset((void *)adc_unpack_last_raw_code, 0, sizeof(adc_unpack_last_raw_code));
    memset((void *)adc_unpack_last_adc_diff_v, 0, sizeof(adc_unpack_last_adc_diff_v));
    memset((void *)adc_unpack_last_voltage_v, 0, sizeof(adc_unpack_last_voltage_v));

    if (s_frame_sem == NULL)
    {
        s_frame_sem = osSemaphoreNew(ADC_RAW_FRAME_RING_DEPTH, 0U, NULL);
        if (s_frame_sem == NULL)
        {
            return ADC_STATUS_ERROR;
        }
    }

    s_unpack_thread = osThreadNew(adc_unpack_thread, NULL, &s_unpack_thread_attr);
    if (s_unpack_thread == NULL)
    {
        return ADC_STATUS_ERROR;
    }

    return ADC_STATUS_OK;
}

uint8_t ADC_Unpack_GetLatestFrame(ADC_FrameData *frame)
{
    uint32_t primask;

    if ((frame == NULL) || (s_latest_valid == 0U))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    memcpy(frame, &s_latest_frame, sizeof(s_latest_frame));
    if (primask == 0U)
    {
        __enable_irq();
    }

    return frame->valid;
}

uint32_t ADC_Unpack_GetFrameCount(void)
{
    return s_unpack_count;
}

void ADC_Driver_FrameReadyCallbackFromISR(void)
{
    if (s_frame_sem != NULL)
    {
        (void)osSemaphoreRelease(s_frame_sem);
    }
}

static void adc_unpack_thread(void *argument)
{
    ADC_RawFrame raw;
    ADC_FrameData frame;

    (void)argument;

    for (;;)
    {
        uint32_t frames_this_wake = 0U;

        if (s_frame_sem != NULL)
        {
            (void)osSemaphoreAcquire(s_frame_sem, osWaitForever);
        }
        else
        {
            osDelay(1U);
        }

        while (ADC_Driver_PopRawFrame(&raw) == ADC_STATUS_OK)
        {
            adc_unpack_convert(&raw, &frame);
            adc_unpack_store_latest(&frame);
            frames_this_wake++;
            if (frames_this_wake >= ADC_UNPACK_MAX_FRAMES_PER_WAKE)
            {
                osThreadYield();
                frames_this_wake = 0U;
            }
        }
    }
}
