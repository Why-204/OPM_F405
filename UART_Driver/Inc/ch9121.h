/**
 * @file    ch9121.h
 * @brief   CH9121 网络串口透传芯片引脚驱动。
 *
 *  本 MCU 通过三根控制线与 CH9121 配合（UART3 走数据透传）：
 *    - ETH_RSTI  (PB3, 输出)  复位输入，低有效；低电平 >=1us，拉高后 ~15ms 可操作
 *    - ETH_CFG   (PB4, 输出)  串口配置模式：拉低进入配置模式，高/悬空为正常透传
 *    - ETH_TCPCS (PA15,输入)  TCP 端口1 连接状态：低电平=已连接（低有效）
 *
 *  引脚定义来自 CubeMX 生成的 main.h。
 */

#ifndef _CH9121_H
#define _CH9121_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* 复位时序（ms），来自 datasheet：拉高后 11~19ms 主机可操作，取 20ms 余量 */
#define CH9121_RESET_LOW_MS 2U
#define CH9121_RESET_READY_MS 20U

    /**
     * @brief 初始化：置 CFG 为高（正常透传模式），并执行一次硬复位。
     *        建议在 opm_uart_init() 中、启动 UART3 通道之前调用。
     */
    void ch9121_init(void);

    /**
     * @brief 硬复位：RSTI 拉低 >=1us 再拉高，等待芯片就绪(~15ms)。
     */
    void ch9121_reset(void);

    /**
     * @brief 进入串口配置模式（CFG 拉低）。进入后可经 UART3 发 CH9121 配置命令。
     */
    void ch9121_enter_config(void);

    /**
     * @brief 退出串口配置模式（CFG 拉高），恢复正常网口<->串口透传。
     */
    void ch9121_exit_config(void);

    /**
     * @brief 查询 TCP（端口1）是否已建立连接。
     * @return true = 已有客户端连接（TCPCS 为低电平）。
     */
    bool ch9121_tcp_connected(void);

    /**
     * @brief 把本机 IP + 本地端口写入 CH9121 内部 EEPROM 并复位生效。
     *
     *  流程（依据《CH9121 串口控制命令 V3.0》）：
     *    CFG 拉低进配置模式(固定 9600bps) → 依次发：
     *      0x11 SET_IP_ADDR(4B) → 0x14 SET_PORT1_SPORT(2B,小端)
     *      → 0x0D SAVE → 0x0E SET_AND_RESET → 恢复波特率 + CFG 拉高。
     *    每条命令芯片回 0xAA 应答。
     *
     *  ⚠ 调用前需先停止该 huart 的 DMA 接收(HAL_UART_DMAStop)；
     *     返回后由调用方重新启动接收(opm_channel_start)。
     *
     * @param huart        CH9121 所接串口句柄（UART3）
     * @param normal_baud  透传波特率，配置完成后用于恢复
     * @param ip           4 字节本机 IP
     * @param port         本地端口
     * @return true = 所有命令均收到 0xAA 应答。
     */
    bool ch9121_write_ip_port(UART_HandleTypeDef *huart, uint32_t normal_baud,
                              const uint8_t ip[4], uint16_t port);

    /**
     * @brief 确保 CH9121 端口1 透传波特率为指定值（建议开机调用一次）。
     *
     *  进配置模式(固定 9600) → 先用 0x71 读回当前波特率：
     *    - 已等于 baud：不动 EEPROM，仅 CFG 拉高退出（避免写损耗）；
     *    - 不一致：0x21 写波特率 → 0x0D 保存 → 0x0E 复位生效。
     *  返回前 STM32 侧 huart 已切到 baud。
     *
     *  ⚠ 需在启动该 huart 的 DMA 接收之前调用（内部会 HAL_UART_Init 重配）。
     *
     * @param huart  CH9121 所接串口句柄（UART3）
     * @param baud   目标透传波特率（应与 huart->Init.BaudRate 一致）
     * @return true = 波特率已为目标值（本就一致或已成功写入）。
     */
    bool ch9121_ensure_baudrate(UART_HandleTypeDef *huart, uint32_t baud);

    /**
     * @brief 确保 CH9121 已配置为确定的 TCP 服务端（建议开机调用一次）。
     *
     *  进配置模式(固定 9600) → 先用 0x60 读工作模式、0x61 读芯片IP：
     *    - 模式已是 TCP 服务端且 IP 匹配：不动 EEPROM，仅退出（避免写损耗）；
     *    - 否则依次写：0x10 模式=TCP服务端 → 0x11 IP → 0x12 子网掩码
     *      → 0x13 网关 → 0x14 本地端口 → 0x0D 保存 → 0x0E 复位生效。
     *  每条命令芯片回 0xAA 应答。返回前 STM32 侧 huart 已切回 normal_baud。
     *
     *  ⚠ 需在启动该 huart 的 DMA 接收之前调用（内部会 HAL_UART_Init 重配）。
     *
     * @param huart        CH9121 所接串口句柄（UART3）
     * @param normal_baud  透传波特率，配置完成后用于恢复
     * @param ip           4 字节本机 IP
     * @param subnet       4 字节子网掩码
     * @param gateway      4 字节网关
     * @param port         本地(监听)端口
     * @return true = 已是目标配置或已成功写入；false = 芯片无应答/写入失败。
     */
    bool ch9121_ensure_tcp_server(UART_HandleTypeDef *huart, uint32_t normal_baud,
                                  const uint8_t ip[4], const uint8_t subnet[4],
                                  const uint8_t gateway[4], uint16_t port);

#ifdef __cplusplus
}
#endif

#endif /* _CH9121_H */
