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
    ADC_Driver.c      ADS1278 control, SPI2 RX-only DMA receive, HAL callbacks
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
3. On every `ADC_DRDY#` falling edge, `ADC_Driver_EXTI_Callback()` starts one RX-only SPI2 DMA transfer for exactly 24 bytes. The driver enables RX DMA before enabling SPI so the first SCLK byte is not lost.
4. The SPI2 RX DMA complete callback records timing-overlap diagnostics, consumes startup discard frames, then pushes valid raw frames into the ring buffer and notifies the unpack thread.
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
- `adc_driver_stats.timing_discard_count`: increments when a new `ADC_DRDY#` overlaps a 24-byte read. The counter is diagnostic only; frames are no longer dropped by this condition.
- `adc_data_valid`: becomes non-zero only after startup discard frames are skipped and at least one frame is unpacked.

Direct watch symbols are also exported for DMA bring-up:

| Symbol | Meaning |
| --- | --- |
| `adc_drdy_cnt` | Counts ADC `DRDY#` falling-edge callbacks. |
| `adc_dma_start_cnt` | Counts SPI2 RX DMA frame-read starts. |
| `adc_dma_complete_cnt` | Counts completed 24-byte SPI2 RX DMA reads. |
| `adc_spi_busy_cnt` | Counts `DRDY#` events that arrived while the previous SPI DMA read was still busy. |
| `adc_missed_drdy_cnt` | Counts `DRDY#` events that could not start a new read because DMA was busy. |
| `adc_timing_discard_cnt` | Timing-overlap warning counter. It increments when `DRDY#` overlaps a 24-byte read; it is diagnostic and does not currently drop the frame. |
| `adc_discard_remaining_cnt` | Startup discard frames still remaining after `SYNC#`. |
| `adc_discarded_frame_cnt` | Frames intentionally discarded during startup settling. |
| `adc_frame_pushed_cnt` | Raw frames pushed into the unpack ring buffer. |
| `adc_frame_popped_cnt` | Raw frames popped by the unpack thread. |
| `adc_last_dma_ndtr` | Last SPI2 RX DMA remaining-byte counter. |
| `adc_last_hal_status` | Last HAL status captured by the ADC driver. |
| `adc_last_spi_state` | Last SPI2 HAL state captured by the ADC driver. |
| `adc_last_spi_error` | Last SPI2 HAL error code. |
| `adc_last_dma_error` | Last SPI2 RX DMA error code. |
| `adc_last_spi_sr` | Last SPI2 status register snapshot. |
| `adc_last_spi_cr1` | Last SPI2 CR1 register snapshot. |
| `adc_last_spi_cr2` | Last SPI2 CR2 register snapshot. |
| `adc_unpack_nonzero_channel_mask` | Bit mask of channels whose latest 3 raw bytes were non-zero. |
| `adc_unpack_last_raw_bytes` | Last 24 raw bytes received from ADS1278. |
| `adc_unpack_last_raw_code` | Last signed 24-bit raw code per channel. |
| `adc_unpack_last_adc_diff_v` | Last ADC differential voltage per channel. |
| `adc_unpack_last_voltage_v` | Last converted input voltage per channel. |
| `adc_task_store_cnt` | Counts latest-frame copies into `adc_ch[]`. |
| `adc_task_last_sequence` | Sequence number of the latest stored frame. |

If `adc_timing_discard_cnt` grows during a static DC input test, the SPI read is crossing the next ADC data-ready edge. With the current `SPI2` clock of 3.375 Mbit/s, a 24-byte single-pin TDM frame takes about 56.9 us to read. That is too slow for ADS1278 high-resolution mode at 27 MHz MCLK, but can still produce stable readings for static DC tests. This counter should be treated as a warning that channel values may be frame-boundary sensitive.

Observed timing hypothesis:

- In high-resolution mode, ADS1278 at 27 MHz MCLK produces a new frame every about 18.96 us.
- With `SPI2` prescaler 2, a 24-byte TDM read takes about 14.2 us. The total frame-transfer time is theoretically short enough, but practical margin is small after EXTI latency, DMA setup, SPI enable delay, and digital-isolator timing. The observed channel-2-and-later misalignment is suspected to be caused by insufficient real timing margin in this fast-read path.
- With larger SPI prescalers in high-resolution mode, the 24-byte read crosses the next `DRDY#`. Prescaler 4 takes about 28.4 us and prescaler 8 takes about 56.9 us, both longer than the 18.96 us high-resolution frame period.
- Low-speed mode increases the ADS1278 frame period to about 94.8 us at 27 MHz MCLK, allowing prescaler 8 to read a full 24-byte frame with timing margin.

## Test Conversion

```text
adc_diff_v = raw_code * 2.5 / 8388608
voltage_v  = ADC_FRONTEND_INPUT_AT_ZERO_DIFF_V + (adc_diff_v - ADC_FRONTEND_ADC_DIFF_ZERO_V) * ADC_FRONTEND_INPUT_V_PER_ADC_DIFF_V
current_a  = 100 nA * 10 ^ ((voltage_v - 1.5) / 0.4)
```

`ADC_FRONTEND_INPUT_V_PER_ADC_DIFF_V` defaults to `-1.0`, matching the measured front-end relationship `input_voltage ~= 2.5V - adc_diff_v` when `adc_diff_v = OUT_P - OUT_N`. The `0.4 V/dec` slope is the current test setting. Future photodiode calibration should replace only the conversion/config layer, not the SPI/DMA capture layer.

## Integration Notes

Hardware changes should still be kept in `OPM_F405.ioc` where possible. The current generated runtime entry points are:

- `main.c` includes `ADC_Task.h` and starts the ADC task before the scheduler runs.
- `stm32f4xx_it.c` routes `EXTI9_5_IRQHandler()` to `HAL_GPIO_EXTI_IRQHandler(ADC_DRDY_Pin)`.
- `ADC_Driver.c` implements the HAL EXTI and SPI callbacks used by the driver.
