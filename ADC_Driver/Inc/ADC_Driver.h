#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "ADC_Types.h"

extern volatile uint32_t adc_drdy_cnt;
extern volatile uint32_t adc_dma_start_cnt;
extern volatile uint32_t adc_dma_complete_cnt;
extern volatile uint32_t adc_dma_error_cnt;
extern volatile uint32_t adc_spi_busy_cnt;
extern volatile uint32_t adc_spi_error_cnt;
extern volatile uint32_t adc_missed_drdy_cnt;
extern volatile uint32_t adc_timing_discard_cnt;
extern volatile uint32_t adc_discarded_frame_cnt;
extern volatile uint32_t adc_discard_remaining_cnt;
extern volatile uint32_t adc_frame_pushed_cnt;
extern volatile uint32_t adc_frame_popped_cnt;
extern volatile uint32_t adc_last_raw_sequence;
extern volatile uint8_t adc_latest_raw_bytes[ADC_FRAME_BYTES];
extern volatile uint32_t adc_last_hal_status;
extern volatile uint32_t adc_last_spi_state;
extern volatile uint32_t adc_last_spi_error;
extern volatile uint32_t adc_last_dma_error;
extern volatile uint32_t adc_last_dma_ndtr;
extern volatile uint32_t adc_last_spi_sr;
extern volatile uint32_t adc_last_spi_cr1;
extern volatile uint32_t adc_last_spi_cr2;

ADC_Status ADC_Driver_Init(SPI_HandleTypeDef *hspi);
ADC_Status ADC_Driver_Start(void);
void ADC_Driver_Stop(void);
void ADC_Driver_Sync(void);
void ADC_Driver_EnableChannels(uint8_t channel_mask);

ADC_Status ADC_Driver_PopRawFrame(ADC_RawFrame *frame);
void ADC_Driver_GetStats(ADC_DriverStats *stats);
uint8_t ADC_Driver_IsRunning(void);

void ADC_Driver_EXTI_Callback(uint16_t GPIO_Pin);

#endif /* ADC_DRIVER_H */
