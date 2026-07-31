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

/* ================================================================ */
/*  连续测量(高速版本) 采样引擎                                       */
/*  粒度固定 100ms：每 25 个 4ms 原始码均值合成 1 个点(先平均后换算)。 */
/* ================================================================ */
/* 放到 CCM(0x10000000，64KB)：仅 CPU 访问、无 DMA，释放主 SRAM。 */
static float s_cap_buf[ADC_CAP_MAX_COUNT][ADC_CHANNEL_COUNT]
    __attribute__((section(".ccmram"), zero_init)); /* CCM，57.6KB */
static volatile uint8_t s_cap_active;
static volatile uint32_t s_cap_target;
static volatile uint32_t s_cap_done;
static int64_t s_cap_acc[ADC_CHANNEL_COUNT];
static uint32_t s_cap_block;

/* 原始码 -> dBmA：与 adc_ch 换算链路一致(先平均后取 log 更准)。 */
static float adc_raw_to_dBmA(int32_t raw_code)
{
    float adc_diff_v = raw_code * ana_cali_cfg.V_per_code;
    float voltage_v = ana_cali_cfg.Vref - adc_diff_v;
    float log_current = (voltage_v - ana_cali_cfg.B) * ana_cali_cfg.K;
    float current_a = powf(10.0f, log_current);
    return 10.0f * log10f(current_a / 1e-3f);
}

void adc_capture_start(uint32_t count)
{
    uint32_t i;
    if (count == 0U)
    {
        return;
    }
    if (count > ADC_CAP_MAX_COUNT)
    {
        count = ADC_CAP_MAX_COUNT;
    }
    s_cap_active = 0U; /* 先停，避免与 tick 竞争 */
    for (i = 0; i < ADC_CHANNEL_COUNT; i++)
    {
        s_cap_acc[i] = 0;
    }
    s_cap_block = 0U;
    s_cap_done = 0U;
    s_cap_target = count;
    s_cap_active = 1U;
}

void adc_capture_stop(void)
{
    s_cap_active = 0U;
}

uint32_t adc_capture_get_done(void)
{
    return s_cap_done;
}

uint32_t adc_capture_read(uint8_t ch_index, uint32_t start, float *out, uint32_t n)
{
    uint32_t done = s_cap_done; /* 追加式：只读 < done 的已提交点，无需锁 */
    uint32_t avail;
    uint32_t i;
    if (ch_index >= ADC_CHANNEL_COUNT || out == NULL)
    {
        return 0U;
    }
    avail = (start < done) ? (done - start) : 0U;
    if (n > avail)
    {
        n = avail;
    }
    for (i = 0; i < n; i++)
    {
        out[i] = s_cap_buf[start + i][ch_index];
    }
    return n;
}

/* 每 4ms 调用一次，传入本次 4ms 原始码均值。满 25 个合成一个 100ms 点。 */
static void adc_capture_tick(const int32_t *raw4ms)
{
    uint32_t i;
    if (!s_cap_active)
    {
        return;
    }
    for (i = 0; i < ADC_CHANNEL_COUNT; i++)
    {
        s_cap_acc[i] += raw4ms[i];
    }
    s_cap_block++;
    if (s_cap_block >= ADC_CAP_BLOCK_FRAMES)
    {
        if (s_cap_done < s_cap_target)
        {
            for (i = 0; i < ADC_CHANNEL_COUNT; i++)
            {
                int32_t avg = (int32_t)(s_cap_acc[i] / (int32_t)ADC_CAP_BLOCK_FRAMES);
                s_cap_buf[s_cap_done][i] = adc_raw_to_dBmA(avg);
            }
            s_cap_done++;
        }
        for (i = 0; i < ADC_CHANNEL_COUNT; i++)
        {
            s_cap_acc[i] = 0;
        }
        s_cap_block = 0U;
        if (s_cap_done >= s_cap_target)
        {
            s_cap_active = 0U;
        }
    }
}

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
            /* dBmA = 10·log10(I / 1mA) —— 在数据源头计算，供 HMI 与协议任务共享 */
            adc_ch[i].dBmA = 10.0f * log10f(adc_ch[i].current_a / 1e-3f);
        }
        /* 连续测量：每 4ms 喂一个 Stage-1 原始码均值给采样引擎 */
        adc_capture_tick(adc_frame4ms.channel);
    }
}
