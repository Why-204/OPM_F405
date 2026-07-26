/**
 * @file    ch9121.c
 * @brief   CH9121 网络串口透传芯片引脚驱动实现，见 ch9121.h。
 *
 *  说明：CH9121 内部集成 TCP/IP 协议栈，做网口<->串口透明传输，
 *  MCU 侧无需实现 TCP/IP；UART3 收发的字节即网络数据。
 *  网络参数(IP/端口/波特率/工作模式)由 CH9121 自身 flash 保存，
 *  通常出厂配置一次即可；运行时可经 CFG 脚进入配置模式修改。
 *
 *  使用 HAL_Delay 做时序延时：依赖 TIM1 时基(uwTick)，
 *  在 RTOS 启动前后均可工作（一次性初始化，阻塞可接受）。
 */

#include "ch9121.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* ================================================================ */
/*  CH9121 串口配置命令（《CH9121 串口控制命令 V3.0》）              */
/*    帧格式：0x57 0xAB <命令码> <参数...>，多字节参数低位在前     */
/*    配置口波特率：CFG 引脚进入时固定 9600bps                     */
/* ================================================================ */
#define CH9121_CFG_BAUD 9600U       /* CFG 配置模式固定波特率 */
#define CH9121_CMD_H0 0x57U         /* 帧头字节0 */
#define CH9121_CMD_H1 0xABU         /* 帧头字节1 */
#define CH9121_CMD_SET_IP 0x11U     /* SET_IP_ADDR      芯片IP(4) */
#define CH9121_CMD_SET_SPORT 0x14U  /* SET_PORT1_SPORT  本地端口(2) */
#define CH9121_CMD_SET_BAUD 0x21U   /* SET_PORT1_BAUDRATE 端口1波特率(4) */
#define CH9121_CMD_GET_BAUD 0x71U   /* GET_PORT1_BAUDRATE 读端口1波特率(4) */
#define CH9121_CMD_SAVE 0x0DU       /* SAVE             保存到EEPROM */
#define CH9121_CMD_RESET 0x0EU      /* SET_AND_RESET    执行并复位 */
#define CH9121_CMD_ACK 0xAAU        /* 芯片应答 */
#define CH9121_CMD_TIMEOUT_MS 100U  /* 单条命令收发超时 */
#define CH9121_CFG_SETTLE_MS 20U    /* CFG 切换后稳定等待 */

void ch9121_enter_config(void)
{
    /* CFG 拉低进入串口配置模式 */
    HAL_GPIO_WritePin(ETH_CFG_GPIO_Port, ETH_CFG_Pin, GPIO_PIN_RESET);
}

void ch9121_exit_config(void)
{
    /* CFG 拉高退出配置模式，恢复正常透传 */
    HAL_GPIO_WritePin(ETH_CFG_GPIO_Port, ETH_CFG_Pin, GPIO_PIN_SET);
}

void ch9121_reset(void)
{
    /* RSTI 低有效：拉低 >=1us（取 2ms 稳妥），再拉高 */
    HAL_GPIO_WritePin(ETH_RSTI_GPIO_Port, ETH_RSTI_Pin, GPIO_PIN_RESET);
    HAL_Delay(CH9121_RESET_LOW_MS);
    HAL_GPIO_WritePin(ETH_RSTI_GPIO_Port, ETH_RSTI_Pin, GPIO_PIN_SET);

    /* 等待芯片就绪（datasheet: 拉高后 11~19ms 可操作） */
    HAL_Delay(CH9121_RESET_READY_MS);
}

void ch9121_init(void)
{
    /* 默认正常透传模式：CFG 高 */
    HAL_GPIO_WritePin(ETH_CFG_GPIO_Port, ETH_CFG_Pin, GPIO_PIN_SET);
    /* 复位保持在非复位态 */
    HAL_GPIO_WritePin(ETH_RSTI_GPIO_Port, ETH_RSTI_Pin, GPIO_PIN_SET);

    /* 执行一次硬复位使配置生效 */
    ch9121_reset();
}

bool ch9121_tcp_connected(void)
{
    /* TCPCS 低电平有效：低=已连接 */
    return (HAL_GPIO_ReadPin(ETH_TCPCS_GPIO_Port, ETH_TCPCS_Pin) == GPIO_PIN_RESET);
}

/* 重配串口波特率（保持其他参数不变） */
static bool ch9121_set_baud(UART_HandleTypeDef *huart, uint32_t baud)
{
    huart->Init.BaudRate = baud;
    return (HAL_UART_Init(huart) == HAL_OK);
}

/* 发送一条配置命令帧并等待 0xAA 应答。
 * 命令帧：0x57 0xAB <cmd> <data...>（data 低位在前，由调用方保证） */
static bool ch9121_send_cmd(UART_HandleTypeDef *huart, uint8_t cmd,
                            const uint8_t *data, uint8_t len)
{
    uint8_t frame[3U + 6U]; /* 帧头(2)+命令(1)+最大参数(IP 4 / MAC 6) */
    uint8_t ack = 0U;

    frame[0] = CH9121_CMD_H0;
    frame[1] = CH9121_CMD_H1;
    frame[2] = cmd;
    if (len > 0U && data != NULL)
    {
        memcpy(&frame[3], data, len);
    }

    if (HAL_UART_Transmit(huart, frame, (uint16_t)(3U + len), CH9121_CMD_TIMEOUT_MS) != HAL_OK)
    {
        return false;
    }
    if (HAL_UART_Receive(huart, &ack, 1U, CH9121_CMD_TIMEOUT_MS) != HAL_OK)
    {
        return false;
    }
    return (ack == CH9121_CMD_ACK);
}

/* 发送 GET 命令帧（0x57 0xAB cmd）并读回 out_len 字节应答 */
static bool ch9121_get_cmd(UART_HandleTypeDef *huart, uint8_t cmd,
                           uint8_t *out, uint8_t out_len)
{
    uint8_t frame[3];
    frame[0] = CH9121_CMD_H0;
    frame[1] = CH9121_CMD_H1;
    frame[2] = cmd;
    if (HAL_UART_Transmit(huart, frame, 3U, CH9121_CMD_TIMEOUT_MS) != HAL_OK)
    {
        return false;
    }
    if (HAL_UART_Receive(huart, out, out_len, CH9121_CMD_TIMEOUT_MS) != HAL_OK)
    {
        return false;
    }
    return true;
}

bool ch9121_ensure_baudrate(UART_HandleTypeDef *huart, uint32_t baud)
{
    uint8_t rd[4] = {0};
    uint32_t cur = 0U;
    bool ok = true;
    bool need_set;

    /* 进配置模式：CFG 拉低 + STM32 侧切 9600（配置口固定 9600，与透传波特率无关） */
    ch9121_enter_config();
    HAL_Delay(CH9121_CFG_SETTLE_MS);
    (void)ch9121_set_baud(huart, CH9121_CFG_BAUD);

    /* 读回端口1 当前透传波特率（0x71，4 字节低位在前） */
    if (ch9121_get_cmd(huart, CH9121_CMD_GET_BAUD, rd, 4U))
    {
        cur = (uint32_t)rd[0] | ((uint32_t)rd[1] << 8)
            | ((uint32_t)rd[2] << 16) | ((uint32_t)rd[3] << 24);
    }
    else
    {
        ok = false; /* 读失败 → 视为不一致，强制写一次 */
    }

    need_set = (cur != baud);

    if (need_set)
    {
        uint8_t bbuf[4];
        bbuf[0] = (uint8_t)(baud & 0xFFU);
        bbuf[1] = (uint8_t)((baud >> 8) & 0xFFU);
        bbuf[2] = (uint8_t)((baud >> 16) & 0xFFU);
        bbuf[3] = (uint8_t)((baud >> 24) & 0xFFU);
        ok = ch9121_send_cmd(huart, CH9121_CMD_SET_BAUD, bbuf, 4U) && ok;
        ok = ch9121_send_cmd(huart, CH9121_CMD_SAVE, NULL, 0U) && ok;
        ok = ch9121_send_cmd(huart, CH9121_CMD_RESET, NULL, 0U) && ok;
    }

    /* STM32 侧切到目标波特率 + 退出配置模式 */
    (void)ch9121_set_baud(huart, baud);
    ch9121_exit_config();

    if (need_set)
    {
        HAL_Delay(CH9121_RESET_READY_MS); /* 写了才需等芯片复位就绪 */
    }
    return ok;
}

bool ch9121_write_ip_port(UART_HandleTypeDef *huart, uint32_t normal_baud,
                          const uint8_t ip[4], uint16_t port)
{
    bool ok = true;
    uint8_t pbuf[2];

    /* 进配置模式：CFG 拉低 + 波特率切 9600 */
    ch9121_enter_config();
    HAL_Delay(CH9121_CFG_SETTLE_MS);
    (void)ch9121_set_baud(huart, CH9121_CFG_BAUD);

    /* 0x11 设置芯片 IP（4 字节） */
    ok = ch9121_send_cmd(huart, CH9121_CMD_SET_IP, ip, 4U) && ok;

    /* 0x14 设置端口1 本地端口（2 字节，低位在前） */
    pbuf[0] = (uint8_t)(port & 0xFFU);
    pbuf[1] = (uint8_t)((port >> 8) & 0xFFU);
    ok = ch9121_send_cmd(huart, CH9121_CMD_SET_SPORT, pbuf, 2U) && ok;

    /* 0x0D 保存至 EEPROM */
    ok = ch9121_send_cmd(huart, CH9121_CMD_SAVE, NULL, 0U) && ok;

    /* 0x0E 执行配置并复位（芯片复位，非 MCU） */
    ok = ch9121_send_cmd(huart, CH9121_CMD_RESET, NULL, 0U) && ok;

    /* 恢复透传波特率 + CFG 拉高退出配置模式 */
    (void)ch9121_set_baud(huart, normal_baud);
    ch9121_exit_config();

    /* 等 CH9121 复位就绪后方可重新透传 */
    HAL_Delay(CH9121_RESET_READY_MS);
    return ok;
}
