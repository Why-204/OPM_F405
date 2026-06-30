#include "ADC_Driver.h"
#include "ADC_Buffer.h"
#include "main.h"
#include <string.h>

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} ADC_GpioPin;

static const ADC_GpioPin s_pwdn_pins[ADC_CHANNEL_COUNT] = {
    {ADC_PWND1_GPIO_Port, ADC_PWND1_Pin},
    {ADC_PWND2_GPIO_Port, ADC_PWND2_Pin},
    {ADC_PWDN3_GPIO_Port, ADC_PWDN3_Pin},
    {ADC_PWND4_GPIO_Port, ADC_PWND4_Pin},
    {ADC_PWND5_GPIO_Port, ADC_PWND5_Pin},
    {ADC_PWND6_GPIO_Port, ADC_PWND6_Pin},
    {ADC_PWND7_GPIO_Port, ADC_PWND7_Pin},
    {ADC_PWND8_GPIO_Port, ADC_PWND8_Pin},
};

static SPI_HandleTypeDef *s_hspi;
static uint8_t s_dma_frame[ADC_FRAME_BYTES];
static uint8_t s_dma_dummy_tx[ADC_FRAME_BYTES];
static volatile uint8_t s_running;
static volatile uint8_t s_dma_busy;
static volatile uint32_t s_sequence;
static volatile uint32_t s_discard_remaining;

static volatile uint32_t s_drdy_count;
static volatile uint32_t s_dma_start_count;
static volatile uint32_t s_dma_complete_count;
static volatile uint32_t s_frames_pushed;
static volatile uint32_t s_frames_popped;
static volatile uint32_t s_ring_overflow_count;
static volatile uint32_t s_spi_busy_count;
static volatile uint32_t s_spi_error_count;
static volatile uint32_t s_spi_tx_dma_missing_count;
static volatile uint32_t s_missed_drdy_count;
static volatile uint32_t s_discarded_frames;
static volatile uint32_t s_last_hal_status;
static volatile uint32_t s_last_spi_state;
static volatile uint32_t s_last_spi_error;
static volatile uint32_t s_last_dma_error;
static volatile uint32_t s_last_dma_ndtr;

volatile uint32_t adc_drdy_cnt;
volatile uint32_t adc_dma_start_cnt;
volatile uint32_t adc_dma_complete_cnt;
volatile uint32_t adc_dma_error_cnt;
volatile uint32_t adc_spi_busy_cnt;
volatile uint32_t adc_spi_error_cnt;
volatile uint32_t adc_spi_tx_dma_missing_cnt;
volatile uint32_t adc_missed_drdy_cnt;
volatile uint32_t adc_discarded_frame_cnt;
volatile uint32_t adc_discard_remaining_cnt;
volatile uint32_t adc_frame_pushed_cnt;
volatile uint32_t adc_frame_popped_cnt;
volatile uint32_t adc_last_hal_status;
volatile uint32_t adc_last_spi_state;
volatile uint32_t adc_last_spi_error;
volatile uint32_t adc_last_dma_error;
volatile uint32_t adc_last_dma_ndtr;

__weak void ADC_Driver_FrameReadyCallbackFromISR(void)
{
}

static uint8_t adc_driver_is_spi2(SPI_HandleTypeDef *hspi)
{
    return (hspi != NULL) && (hspi->Instance == SPI2);
}

static uint32_t adc_driver_dma_error(const SPI_HandleTypeDef *hspi)
{
    uint32_t rx_error = (hspi->hdmarx != NULL) ? hspi->hdmarx->ErrorCode : 0xFFFFFFFFU;
    uint32_t tx_error = (hspi->hdmatx != NULL) ? hspi->hdmatx->ErrorCode : 0xFFFFFFFFU;

    return (rx_error != HAL_DMA_ERROR_NONE) ? rx_error : tx_error;
}

static uint32_t adc_driver_dma_ndtr(const SPI_HandleTypeDef *hspi)
{
    return (hspi->hdmarx != NULL) ? __HAL_DMA_GET_COUNTER(hspi->hdmarx) : 0xFFFFFFFFU;
}

static void adc_driver_update_last_status(SPI_HandleTypeDef *hspi, HAL_StatusTypeDef hal_status)
{
    s_last_hal_status = (uint32_t)hal_status;
    s_last_spi_state = (uint32_t)hspi->State;
    s_last_spi_error = hspi->ErrorCode;
    s_last_dma_error = adc_driver_dma_error(hspi);
    s_last_dma_ndtr = adc_driver_dma_ndtr(hspi);
    adc_last_hal_status = s_last_hal_status;
    adc_last_spi_state = s_last_spi_state;
    adc_last_spi_error = s_last_spi_error;
    adc_last_dma_error = s_last_dma_error;
    adc_last_dma_ndtr = s_last_dma_ndtr;
}

static void adc_driver_handle_dma_complete(SPI_HandleTypeDef *hspi)
{
    s_dma_busy = 0U;
    s_dma_complete_count++;
    adc_dma_complete_cnt = s_dma_complete_count;
    adc_driver_update_last_status(hspi, HAL_OK);

    if (s_discard_remaining > 0U)
    {
        s_discard_remaining--;
        s_discarded_frames++;
        adc_discarded_frame_cnt = s_discarded_frames;
        adc_discard_remaining_cnt = s_discard_remaining;
        return;
    }

    s_sequence++;
    if (ADC_Buffer_PushFromISR(s_dma_frame, s_sequence, HAL_GetTick()) == ADC_STATUS_OK)
    {
        s_frames_pushed++;
        adc_frame_pushed_cnt = s_frames_pushed;
        ADC_Driver_FrameReadyCallbackFromISR();
    }
    else
    {
        s_ring_overflow_count++;
    }
}

ADC_Status ADC_Driver_Init(SPI_HandleTypeDef *hspi)
{
    if (!adc_driver_is_spi2(hspi))
    {
        return ADC_STATUS_INVALID_PARAM;
    }

    s_hspi = hspi;
    s_running = 0U;
    s_dma_busy = 0U;
    s_sequence = 0U;
    s_discard_remaining = 0U;

    s_drdy_count = 0U;
    s_dma_start_count = 0U;
    s_dma_complete_count = 0U;
    s_frames_pushed = 0U;
    s_frames_popped = 0U;
    s_ring_overflow_count = 0U;
    s_spi_busy_count = 0U;
    s_spi_error_count = 0U;
    s_spi_tx_dma_missing_count = 0U;
    s_missed_drdy_count = 0U;
    s_discarded_frames = 0U;
    s_last_hal_status = HAL_OK;
    s_last_spi_state = HAL_SPI_STATE_RESET;
    s_last_spi_error = HAL_SPI_ERROR_NONE;
    s_last_dma_error = HAL_DMA_ERROR_NONE;
    s_last_dma_ndtr = 0U;

    adc_drdy_cnt = 0U;
    adc_dma_start_cnt = 0U;
    adc_dma_complete_cnt = 0U;
    adc_dma_error_cnt = 0U;
    adc_spi_busy_cnt = 0U;
    adc_spi_error_cnt = 0U;
    adc_spi_tx_dma_missing_cnt = 0U;
    adc_missed_drdy_cnt = 0U;
    adc_discarded_frame_cnt = 0U;
    adc_discard_remaining_cnt = 0U;
    adc_frame_pushed_cnt = 0U;
    adc_frame_popped_cnt = 0U;
    adc_last_hal_status = HAL_OK;
    adc_last_spi_state = HAL_SPI_STATE_RESET;
    adc_last_spi_error = HAL_SPI_ERROR_NONE;
    adc_last_dma_error = HAL_DMA_ERROR_NONE;
    adc_last_dma_ndtr = 0U;

    memset(s_dma_frame, 0, sizeof(s_dma_frame));
    memset(s_dma_dummy_tx, 0xFF, sizeof(s_dma_dummy_tx));
    ADC_Buffer_Reset();

    HAL_GPIO_WritePin(ADC_SYNC_GPIO_Port, ADC_SYNC_Pin, GPIO_PIN_SET);
    adc_discard_remaining_cnt = s_discard_remaining;
    ADC_Driver_EnableChannels(0x00U);

    return ADC_STATUS_OK;
}

ADC_Status ADC_Driver_Start(void)
{
    if (s_hspi == NULL)
    {
        return ADC_STATUS_ERROR;
    }

    s_running = 0U;
    s_dma_busy = 0U;
    ADC_Buffer_Reset();
    ADC_Driver_EnableChannels(0xFFU);
    HAL_Delay(ADC_POWER_UP_DELAY_MS);
    ADC_Driver_Sync();
    s_running = 1U;

    return ADC_STATUS_OK;
}

void ADC_Driver_Stop(void)
{
    s_running = 0U;
    if ((s_hspi != NULL) && (s_dma_busy != 0U))
    {
        (void)HAL_SPI_Abort(s_hspi);
    }
    s_dma_busy = 0U;
}

void ADC_Driver_Sync(void)
{
    HAL_GPIO_WritePin(ADC_SYNC_GPIO_Port, ADC_SYNC_Pin, GPIO_PIN_RESET);
    HAL_Delay(ADC_SYNC_LOW_TIME_MS);
    HAL_GPIO_WritePin(ADC_SYNC_GPIO_Port, ADC_SYNC_Pin, GPIO_PIN_SET);
    s_discard_remaining = ADC_STARTUP_DISCARD_FRAMES;
    adc_discard_remaining_cnt = s_discard_remaining;
}

void ADC_Driver_EnableChannels(uint8_t channel_mask)
{
    uint32_t i;

    for (i = 0U; i < ADC_CHANNEL_COUNT; i++)
    {
        GPIO_PinState state = ((channel_mask & (1U << i)) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
        HAL_GPIO_WritePin(s_pwdn_pins[i].port, s_pwdn_pins[i].pin, state);
    }
}

ADC_Status ADC_Driver_PopRawFrame(ADC_RawFrame *frame)
{
    ADC_Status status = ADC_Buffer_Pop(frame);
    if (status == ADC_STATUS_OK)
    {
        s_frames_popped++;
        adc_frame_popped_cnt = s_frames_popped;
    }
    return status;
}

void ADC_Driver_GetStats(ADC_DriverStats *stats)
{
    uint32_t primask;

    if (stats == NULL)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    stats->drdy_count = s_drdy_count;
    stats->dma_start_count = s_dma_start_count;
    stats->dma_complete_count = s_dma_complete_count;
    stats->frames_pushed = s_frames_pushed;
    stats->frames_popped = s_frames_popped;
    stats->ring_overflow_count = s_ring_overflow_count;
    stats->spi_busy_count = s_spi_busy_count;
    stats->spi_error_count = s_spi_error_count;
    stats->spi_tx_dma_missing_count = s_spi_tx_dma_missing_count;
    stats->missed_drdy_count = s_missed_drdy_count;
    stats->discarded_frames = s_discarded_frames;
    stats->last_hal_status = s_last_hal_status;
    stats->last_spi_state = s_last_spi_state;
    stats->last_spi_error = s_last_spi_error;
    stats->last_dma_error = s_last_dma_error;
    stats->last_dma_ndtr = s_last_dma_ndtr;
    stats->running = s_running;
    stats->dma_busy = s_dma_busy;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint8_t ADC_Driver_IsRunning(void)
{
    return s_running;
}

void ADC_Driver_EXTI_Callback(uint16_t GPIO_Pin)
{
    HAL_StatusTypeDef hal_status;

    if (GPIO_Pin != ADC_DRDY_Pin)
    {
        return;
    }

    if ((s_running == 0U) || (s_hspi == NULL))
    {
        return;
    }

    s_drdy_count++;
    adc_drdy_cnt = s_drdy_count;

    if (s_dma_busy != 0U)
    {
        s_spi_busy_count++;
        s_missed_drdy_count++;
        adc_spi_busy_cnt = s_spi_busy_count;
        adc_missed_drdy_cnt = s_missed_drdy_count;
        return;
    }

    if ((s_hspi->hdmarx == NULL) || (s_hspi->hdmatx == NULL))
    {
        s_spi_error_count++;
        adc_spi_error_cnt = s_spi_error_count;
        if (s_hspi->hdmatx == NULL)
        {
            s_spi_tx_dma_missing_count++;
            adc_spi_tx_dma_missing_cnt = s_spi_tx_dma_missing_count;
        }
        adc_driver_update_last_status(s_hspi, HAL_ERROR);
        return;
    }

    s_dma_busy = 1U;
    s_dma_start_count++;
    adc_dma_start_cnt = s_dma_start_count;
    hal_status = HAL_SPI_TransmitReceive_DMA(s_hspi, s_dma_dummy_tx, s_dma_frame, ADC_FRAME_BYTES);
    adc_driver_update_last_status(s_hspi, hal_status);
    if (hal_status != HAL_OK)
    {
        s_dma_busy = 0U;
        s_spi_error_count++;
        adc_spi_error_cnt = s_spi_error_count;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    ADC_Driver_EXTI_Callback(GPIO_Pin);
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if ((s_hspi == NULL) || (hspi != s_hspi))
    {
        return;
    }

    adc_driver_handle_dma_complete(hspi);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if ((s_hspi == NULL) || (hspi != s_hspi))
    {
        return;
    }

    adc_driver_handle_dma_complete(hspi);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if ((s_hspi == NULL) || (hspi != s_hspi))
    {
        return;
    }

    s_dma_busy = 0U;
    s_spi_error_count++;
    adc_spi_error_cnt = s_spi_error_count;
    adc_dma_error_cnt++;
    adc_driver_update_last_status(hspi, HAL_ERROR);
}
