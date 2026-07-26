/**
 * @file    UART_Task.h
 * @brief   光功率计双串口通信任务：UART3(CH9121 网口) 与 UART5(RS232)。
 *
 *  每个 UART 由一个独立任务服务，共用 26 条命令处理逻辑(opm_protocol)。
 *  两个通道实例在 UART_Task.c 中定义，供中断处理(stm32f4xx_it.c)引用。
 */

#ifndef _UART_TASK_H
#define _UART_TASK_H

#include "opm_channel.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* 两个通道实例（定义在 UART_Task.c），中断处理函数按引用调用其 IDLE 处理 */
    extern OpmChannel g_opm_ch_uart3; /* 网口(CH9121 透传) */
    extern OpmChannel g_opm_ch_uart5; /* RS232 (TTL->232)  */

    /**
     * @brief 初始化设备状态、CH9121、两个串口通道，并创建两个接收任务。
     *        在 main.c 的 RTOS_THREADS 区调用（与 adc_task_init/hmi_task_init 并列）。
     */
    void opm_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _UART_TASK_H */
