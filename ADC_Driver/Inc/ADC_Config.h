#ifndef ADC_CONFIG_H
#define ADC_CONFIG_H

#include <stdint.h>

#define ADC_CHANNEL_COUNT                  8U
#define ADC_BYTES_PER_CHANNEL              3U
#define ADC_FRAME_BYTES                    (ADC_CHANNEL_COUNT * ADC_BYTES_PER_CHANNEL)

#define ADC_RAW_FRAME_RING_DEPTH           32U
#define ADC_STARTUP_DISCARD_FRAMES         128U

#define ADC_POWER_UP_DELAY_MS              10U
#define ADC_SYNC_LOW_TIME_MS               1U

#define ADC_VREF_V                         2.5f
#define ADC_CODE_FULL_SCALE                8388607.0f

/* ADC_diff = OUT_P - OUT_N. Measured input ~= 2.5V - ADC_diff. */
#define ADC_FRONTEND_INPUT_AT_ZERO_DIFF_V  2.5f
#define ADC_FRONTEND_ADC_DIFF_ZERO_V       0.0f
#define ADC_FRONTEND_INPUT_V_PER_ADC_DIFF_V (-1.0f)
#define ADC_LOG_REF_VOLTAGE_V              1.5f
#define ADC_LOG_REF_CURRENT_A              1.0e-7f
#define ADC_LOG_SLOPE_V_PER_DEC            0.4f
#define ADC_ENABLE_LOG_CURRENT_CONVERSION  0U

#define ADC_TASK_PERIOD_MS                 10U
#define ADC_UNPACK_MAX_FRAMES_PER_WAKE     4U
#define ADC_UNPACK_THREAD_STACK_BYTES      256U
#define ADC_TASK_THREAD_STACK_BYTES        1536U

#define ADC_MAX_FRAME_LENGTH               100U

#endif /* ADC_CONFIG_H */
