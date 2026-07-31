/**
 * @file    UART_Task.c
 * @brief   光功率计双串口通信任务实现。
 *
 *  架构：
 *    - g_opm_ch_uart3 / g_opm_ch_uart5：两个独立通道实例，各自缓冲互不干扰。
 *    - 每个通道一个任务：等 RX_RDY 标志 → 组帧 → 分发 → 原路回复。
 *    - g_opm_dev 为两任务共享的设备状态，用 s_dev_mutex 互斥保护。
 *    - 收到 BOOT 命令：发完应答后延时复位整机。
 *    - UART3 数据经 CH9121 透传到网口，协议字节与 UART5 完全一致。
 */

#include "UART_Task.h"
#include "opm_protocol.h"
#include "ch9121.h"
#include "cmsis_os.h"
#include "main.h"

/* CubeMX 生成的串口句柄 */
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart5;

/* ================================================================ */
/*  通道实例 / 共享保护                                              */
/* ================================================================ */
OpmChannel g_opm_ch_uart3; /* 网口(CH9121) */
OpmChannel g_opm_ch_uart5; /* RS232        */

static osMutexId_t s_dev_mutex; /* 保护共享的 g_opm_dev */

static const osMutexAttr_t s_dev_mutex_attr = {
    .name = "opm_dev",
};

/* ================================================================ */
/*  任务句柄 / 属性                                                  */
/* ================================================================ */
static osThreadId_t s_uart3_thread;
static osThreadId_t s_uart5_thread;

static const osThreadAttr_t s_uart3_attr = {
    .name = "opm_uart3",
    .stack_size = 2048U,
    .priority = (osPriority_t)osPriorityNormal,
};

static const osThreadAttr_t s_uart5_attr = {
    .name = "opm_uart5",
    .stack_size = 2048U,
    .priority = (osPriority_t)osPriorityNormal,
};

/* BOOT 应答发完后到复位的等待时间(ms)，确保应答字节发出 */
#define OPM_REBOOT_DELAY_MS 50U

/* WRIP/WRPT 应答发出后、复位 CH9121 前的等待(ms)，
 * 让 CH9121 有时间把应答透传到网络端，避免上位机收不到 OK */
#define OPM_NETCFG_PRE_DELAY_MS 100U

/* ================================================================ */
/*  通道任务：请求-应答循环                                          */
/* ================================================================ */
static void opm_channel_task(void *arg)
{
    OpmChannel *ch = (OpmChannel *)arg;
    OpmFrame req;
    uint8_t resp[OPM_MAX_FRAME_LEN];

    /* CH9121 一次性配置(仅 UART3 网口通道)：放在调度器启动后执行，
     * 避免其阻塞式 HAL_Delay/串口收发在 osKernelStart 前拖住整机启动，
     * 导致 HMI 等任务起不来。 */
    if (ch == &g_opm_ch_uart3)
    {
        /* 网络参数固定为确定的 TCP 服务端，便于上位机直连验证。
         * IP/子网/网关须同网段自洽；IP/端口取 g_opm_dev（与 RDIP/RDPT 上报值一致）。 */
        static const uint8_t netcfg_subnet[4] = {255U, 255U, 255U, 0U};
        static const uint8_t netcfg_gateway[4] = {192U, 168U, 1U, 1U};
        ch9121_init();
        (void)ch9121_ensure_tcp_server(ch->huart, ch->huart->Init.BaudRate,
                                       g_opm_dev.ip, netcfg_subnet,
                                       netcfg_gateway, g_opm_dev.port);
    }

    /* 在本任务内武装循环 DMA 接收(必须在 CH9121 重配 UART3 之后) */
    opm_channel_start(ch);

    for (;;)
    {
        /* 等 ISR 通知有新数据（也周期性醒来兜底处理残留帧） */
        (void)osThreadFlagsWait(OPM_CH_RX_RDY, osFlagsWaitAny, 100U);

        /* 一次可能收到多帧，全部处理完 */
        while (opm_channel_read_frame(ch, &req))
        {
            bool reboot = false;
            bool netcfg = false;
            uint8_t netcfg_ip[4] = {0};
            uint16_t netcfg_port = 0U;
            uint16_t n;

            /* 共享设备状态：加锁分发 */
            osMutexAcquire(s_dev_mutex, osWaitForever);
            n = opm_dispatch(&req, resp, (uint16_t)sizeof(resp));
            reboot = opm_reboot_pending();
            netcfg = opm_netcfg_pending();
            if (netcfg)
            {
                /* 在锁内快照新 IP/端口，避免与另一通道任务竞争 */
                memcpy(netcfg_ip, g_opm_dev.ip, 4U);
                netcfg_port = g_opm_dev.port;
            }
            osMutexRelease(s_dev_mutex);

            /* 原路回复（谁收到命令就从谁回） */
            if (n > 0U)
            {
                opm_channel_send(ch, resp, n);
            }

            /* WRIP/WRPT：把新网络参数写入 CH9121（始终驱动 UART3，无论命令来自哪个口）。
             * 先等一会让 CH9121 把应答透传到网络端，再进配置模式复位芯片。 */
            if (netcfg)
            {
                uint32_t normal_baud = huart3.Init.BaudRate;
                osDelay(OPM_NETCFG_PRE_DELAY_MS);
                HAL_UART_DMAStop(&huart3);
                (void)ch9121_write_ip_port(&huart3, normal_baud, netcfg_ip, netcfg_port);
                opm_channel_start(&g_opm_ch_uart3); /* 重新武装循环 DMA 接收 */
            }

            /* BOOT：应答已发出，延时后复位整机 */
            if (reboot)
            {
                osDelay(OPM_REBOOT_DELAY_MS);
                NVIC_SystemReset();
            }
        }
    }
}

/* ================================================================ */
/*  初始化                                                           */
/* ================================================================ */
void opm_task_init(void)
{
    /* 1) 设备状态 + 共享互斥锁 */
    opm_device_state_init();
    if (s_dev_mutex == NULL)
    {
        s_dev_mutex = osMutexNew(&s_dev_mutex_attr);
    }

    /* 2) 初始化两个通道实例 */
    opm_channel_init(&g_opm_ch_uart3, &huart3, "UART3");
    opm_channel_init(&g_opm_ch_uart5, &huart5, "UART5");

    /* 3) 创建两个接收任务 */
    if (s_uart3_thread == NULL)
    {
        s_uart3_thread = osThreadNew(opm_channel_task, &g_opm_ch_uart3, &s_uart3_attr);
    }
    if (s_uart5_thread == NULL)
    {
        s_uart5_thread = osThreadNew(opm_channel_task, &g_opm_ch_uart5, &s_uart5_attr);
    }

    /* 4) 绑定任务句柄(ISR 据此发标志)。
     *    CH9121 配置与循环 DMA 接收的启动改到各自任务内执行(见 opm_channel_task)，
     *    使 opm_task_init 保持非阻塞，保证 osKernelStart 能立即运行、
     *    其余任务(含 HMI)正常启动。 */
    opm_channel_bind_task(&g_opm_ch_uart3, s_uart3_thread);
    opm_channel_bind_task(&g_opm_ch_uart5, s_uart5_thread);
}
