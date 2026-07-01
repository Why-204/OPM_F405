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
3. On every `ADC_DRDY#` falling edge, `ADC_Driver_EXTI_Callback()` directly reloads the preconfigured dummy-TX and RX DMA registers for exactly 24 bytes. This avoids the per-frame HAL DMA setup latency, and TX DMA limits each read to exactly 192 SCLK edges. The unused MOSI signal is not routed to a GPIO because `PB15` remains the ADC `SYNC` output.
4. The priority-4 SPI2 RX DMA callback drops any frame that overlapped a later `DRDY`, consumes startup discard frames, and pushes valid raw frames into the ring buffer. It then pends the priority-5 SPI2 TX DMA IRQ, which safely notifies the unpack thread through the RTOS semaphore.
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
- `adc_driver_stats.timing_discard_count`: increments when a new `ADC_DRDY#` overlaps a 24-byte read. Every overlapped frame is dropped before unpacking.
- `adc_data_valid`: becomes non-zero only after startup discard frames are skipped and at least one frame is unpacked.

Direct watch symbols are also exported for DMA bring-up:

- `adc_drdy_cnt`
- `adc_dma_start_cnt`
- `adc_dma_complete_cnt`
- `adc_spi_busy_cnt`
- `adc_spi_error_cnt`
- `adc_missed_drdy_cnt`
- `adc_timing_discard_cnt`
- `adc_discarded_frame_cnt`
- `adc_discard_remaining_cnt`
- `adc_frame_pushed_cnt`
- `adc_frame_popped_cnt`
- `adc_last_hal_status`
- `adc_last_spi_state`
- `adc_last_spi_error`
- `adc_last_dma_error`
- `adc_last_dma_ndtr`
- `adc_last_spi_sr`
- `adc_last_spi_cr1`
- `adc_last_spi_cr2`
- `adc_drdy_period_cycles`
- `adc_dma_duration_cycles`
- `adc_overlap_dma_ndtr`
- `adc_system_core_clock_hz`
- `adc_unpack_nonzero_channel_mask`
- `adc_unpack_last_raw_bytes`
- `adc_unpack_last_raw_code`
- `adc_task_store_cnt`
- `adc_task_last_sequence`

The ADC EXTI and RX DMA completion run at priority 4 so acquisition is not delayed by FreeRTOS priority-5 critical sections. The RTOS semaphore release is deferred to the priority-5 TX DMA IRQ. With the current `SPI2` clock of 13.5 Mbit/s, a 24-byte single-pin TDM frame takes about 14.2 us to read. This is sufficient for ADS1278 high-resolution or low-power mode at 27 MHz MCLK, but not for high-speed mode at 27 MHz MCLK.

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
