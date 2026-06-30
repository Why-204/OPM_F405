# OPM_F405

STM32F405 optical power meter firmware.

## Current Structure

```text
Core/                 CubeMX generated startup, HAL init, IRQ files
Drivers/              STM32 HAL/CMSIS driver package
Middlewares/          FreeRTOS middleware
HMI_Driver/           Existing HMI screen driver, kept unchanged
Tasks/
  HMI_Task.*          Existing HMI task files
  ADC_Task.*          ADC application task and latest 8-channel data cache
ADC_Driver/
  Inc/
    ADC_Config.h      ADC frame size, buffer depth, voltage/current test constants
    ADC_Types.h       Raw frame, channel data, frame data, driver statistics
    ADC_Buffer.h      Raw frame ring buffer interface
    ADC_Driver.h      ADS1278 SPI2/DMA/GPIO control interface
    ADC_Unpack.h      Raw frame unpack and conversion thread interface
  Src/
    ADC_Buffer.c      Lock-protected raw frame ring buffer
    ADC_Driver.c      ADS1278 control, SPI2 full-duplex DMA receive, HAL callbacks
    ADC_Unpack.c      24-bit unpack, voltage conversion, test current conversion
```

## Hardware Notes

- IOC uses `SPI2` for the ADC link: `PB13` is `SPI2_SCK`, `PB14` is `SPI2_MISO`.
- The schematic still labels the MCU-side nets as `SPI1_CLK` and `SPI1_MISO`; keep this historical naming error in mind, but firmware must operate only on `SPI2`.
- ADS1278 is treated as an 8-channel, 24-bit ADC using single-pin TDM output on `DOUT1`, so one raw frame is 24 bytes.
- `ADC_DRDY#` is intended to trigger a read when it falls low. The IOC marks `PC6` as falling-edge EXTI.
- `ADC_SYNC#` is pulsed by the driver before acquisition starts, then the first frames are discarded to let the converter settle.
- The 8 `PWDN#` pins are controlled by the driver as an 8-bit channel mask. A set bit enables the channel.

## Execution Logic

1. `main.c` calls `adc_task_init()` once after `osKernelInitialize()` and before `osKernelStart()`.
2. `ADC_Task` starts the unpack thread, initializes `ADC_Driver` with `hspi2`, enables all ADC channels, and starts acquisition.
3. On every `ADC_DRDY#` falling edge, `ADC_Driver_EXTI_Callback()` starts one `HAL_SPI_TransmitReceive_DMA()` transfer for exactly 24 bytes. The TX buffer is dummy data used only to keep `SPI2_SCK` running.
4. `HAL_SPI_TxRxCpltCallback()` pushes the complete raw frame into the ring buffer and notifies the unpack thread.
5. `ADC_Unpack` converts each 3-byte signed 24-bit code into voltage and then into a test current value.
6. `ADC_Task` periodically copies the latest frame into `adc_latest_frame` and `adc_ch[8]`.

## Runtime Checks

Useful debugger variables:

- `adc_task_state`: task lifecycle state.
- `adc_task_alive_tick`: increments while the ADC task loop is running.
- `adc_task_last_status`: last init/start status.
- `adc_driver_stats.running`: driver acquisition state.
- `adc_driver_stats.drdy_count`: increments on `ADC_DRDY#` EXTI callbacks.
- `adc_driver_stats.dma_complete_count`: increments after SPI2 DMA completes a raw frame.
- `adc_data_valid`: becomes non-zero only after startup discard frames are skipped and at least one frame is unpacked.

Direct watch symbols are also exported for DMA bring-up:

- `adc_drdy_cnt`
- `adc_dma_start_cnt`
- `adc_dma_complete_cnt`
- `adc_spi_busy_cnt`
- `adc_spi_error_cnt`
- `adc_spi_tx_dma_missing_cnt`
- `adc_missed_drdy_cnt`
- `adc_discarded_frame_cnt`
- `adc_discard_remaining_cnt`
- `adc_frame_pushed_cnt`
- `adc_frame_popped_cnt`
- `adc_last_hal_status`
- `adc_last_spi_state`
- `adc_last_spi_error`
- `adc_last_dma_error`
- `adc_last_dma_ndtr`

## Test Conversion

```text
adc_diff_v = raw_code * 2.5 / 8388608
voltage_v  = adc_diff_v + 1.5
current_a  = 100 nA * 10 ^ ((voltage_v - 1.5) / 0.4)
```

The `0.4 V/dec` slope is the current test setting. Future photodiode calibration should replace only the conversion/config layer, not the SPI/DMA capture layer.

## Integration Notes

Hardware changes should still be kept in `OPM_F405.ioc` where possible. The current generated runtime entry points are:

- `main.c` includes `ADC_Task.h` and starts the ADC task before the scheduler runs.
- `stm32f4xx_it.c` routes `EXTI9_5_IRQHandler()` to `HAL_GPIO_EXTI_IRQHandler(ADC_DRDY_Pin)`.
- `ADC_Driver.c` implements the HAL EXTI and SPI callbacks used by the driver.

For full-duplex SPI2 DMA acquisition, CubeMX must generate both `SPI2_RX` and `SPI2_TX` DMA handles. If `ADC_DRDY#` is counting but `adc_dma_start_cnt` does not increase and `adc_spi_tx_dma_missing_cnt` increases, enable `SPI2_TX` DMA in CubeMX and regenerate.
