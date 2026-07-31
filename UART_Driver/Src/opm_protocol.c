/**
 * @file    opm_protocol.c
 * @brief   多通道光功率计通信协议实现：校验、拆/组包、26 条命令分发。
 *          见 opm_protocol.h 帧格式说明。本 MCU 为下位机（被动应答）。
 *
 *  光功率(dBm)取自 ADC_Task 的 adc_ch[i].dBmA。
 *  并发保护由调用方(task 层)负责。
 */

#include "opm_protocol.h"
#include "ADC_Task.h"
#include "ADC_Unpack.h"

/* ================================================================ */
/*  全局设备状态 / 重启标志                                          */
/* ================================================================ */
OpmDeviceState g_opm_dev;
static volatile bool s_reboot_pending = false;
static volatile bool s_netcfg_pending = false;

bool opm_reboot_pending(void) { return s_reboot_pending; }

/* 读取并清除「网络参数(IP/端口)需下发 CH9121」标志。
 * 由 WRIP/WRPT 置位；任务层发完应答后查询并把新参数写入 CH9121。 */
bool opm_netcfg_pending(void)
{
    bool p = s_netcfg_pending;
    s_netcfg_pending = false;
    return p;
}

/* 手册第 12) 条列出的标定波长(nm) */
static const uint16_t k_cal_wl_default[] = {
    850, 980, 1270, 1290, 1295, 1300, 1305, 1310,
    1330, 1490, 1530, 1550, 1570, 1620};
#define K_CAL_WL_DEFAULT_CNT (sizeof(k_cal_wl_default) / sizeof(k_cal_wl_default[0]))

void opm_device_state_init(void)
{
    OpmDeviceState *d = &g_opm_dev;
    memset(d, 0, sizeof(*d));

    memcpy(d->product_name, "PM4177", 6);
    memcpy(d->serial, "PM2017071801", 12);
    d->version[0] = 1; /* 硬件主 */
    d->version[1] = 0; /* 硬件副 */
    d->version[2] = 2; /* 软件主 */
    d->version[3] = 1; /* 软件副 */

    d->mac[0] = 0xAA; d->mac[1] = 0xBB; d->mac[2] = 0xCC;
    d->mac[3] = 0xDD; d->mac[4] = 0xEE; d->mac[5] = 0xFF;

    d->ip[0] = 192; d->ip[1] = 168; d->ip[2] = 1; d->ip[3] = 200;
    d->port = 8888;

    d->channel_count = (ADC_CHANNEL_COUNT <= OPM_MAX_CHANNELS)
                           ? (uint8_t)ADC_CHANNEL_COUNT
                           : (uint8_t)OPM_MAX_CHANNELS;

    d->cal_wl_count = (uint8_t)((K_CAL_WL_DEFAULT_CNT <= OPM_MAX_CAL_WL)
                                    ? K_CAL_WL_DEFAULT_CNT
                                    : OPM_MAX_CAL_WL);
    for (uint8_t i = 0; i < d->cal_wl_count; i++)
    {
        d->cal_wl[i] = k_cal_wl_default[i];
    }

    for (uint8_t i = 0; i < OPM_MAX_CHANNELS; i++)
    {
        d->work_wl[i] = 1310;   /* 默认工作波长     */
        d->sample_us[i] = 100;  /* 默认采样平均时间 */
    }

    d->meas_mode = OPM_MEAS_NORMAL;
}

/* ================================================================ */
/*  校验 / 拆包 / 组包                                               */
/* ================================================================ */
uint8_t opm_checksum(const uint8_t *buf, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++)
    {
        sum += buf[i];
    }
    return (uint8_t)(sum & 0xFFU);
}

int opm_frame_parse(const uint8_t *buf, uint16_t len, OpmFrame *out)
{
    if (len < (OPM_HEADER_LEN + OPM_LENGTH_LEN))
    {
        return -1; /* 不足以读出包长度 */
    }
    if (buf[0] != OPM_FRAME_HEADER)
    {
        return -2; /* 包头错误 */
    }

    uint16_t length_field = opm_rd_u16le(&buf[OPM_HEADER_LEN]);
    uint16_t total = (uint16_t)(length_field + OPM_HEADER_LEN + OPM_LENGTH_LEN);

    if (total < OPM_OVERHEAD)
    {
        return -3; /* 长度非法（连命令字+校验和都不够） */
    }
    if (len < total)
    {
        return -1; /* 数据不足 */
    }

    uint8_t chk_recv = buf[total - 1];
    uint8_t chk_calc = opm_checksum(buf, (uint16_t)(total - 1));
    if (chk_recv != chk_calc)
    {
        return -3; /* 校验和错误 */
    }

    out->cmd = OPM_CMD4(buf[3], buf[4], buf[5], buf[6]);
    out->data_len = (uint16_t)(total - OPM_OVERHEAD);
    out->data = (out->data_len > 0) ? &buf[OPM_HEADER_LEN + OPM_LENGTH_LEN + OPM_CMDWORD_LEN] : NULL;
    return (int)total;
}

uint16_t opm_frame_build(uint8_t *out, uint16_t out_cap,
                         uint32_t cmd, const uint8_t *data, uint16_t data_len)
{
    uint16_t total = (uint16_t)(data_len + OPM_OVERHEAD);
    if (out == NULL || total > out_cap)
    {
        return 0;
    }

    uint16_t length_field = (uint16_t)(data_len + OPM_CMDWORD_LEN + OPM_CHECKSUM_LEN);
    out[0] = OPM_FRAME_HEADER;
    opm_wr_u16le(&out[1], length_field);
    out[3] = (uint8_t)((cmd >> 24) & 0xFFU);
    out[4] = (uint8_t)((cmd >> 16) & 0xFFU);
    out[5] = (uint8_t)((cmd >> 8) & 0xFFU);
    out[6] = (uint8_t)(cmd & 0xFFU);
    if (data_len > 0 && data != NULL)
    {
        memcpy(&out[7], data, data_len);
    }
    out[7 + data_len] = opm_checksum(out, (uint16_t)(7 + data_len));
    return total;
}

uint16_t opm_build_error(uint8_t *out, uint16_t out_cap)
{
    /* 命令解析错误应答：AA 04 00 45 52 52 97（命令字为 3 字节 "ERR"） */
    if (out == NULL || out_cap < 7)
    {
        return 0;
    }
    out[0] = OPM_FRAME_HEADER;
    out[1] = 0x04;
    out[2] = 0x00;
    out[3] = 'E';
    out[4] = 'R';
    out[5] = 'R';
    out[6] = opm_checksum(out, 6);
    return 7;
}

/* ================================================================ */
/*  内部辅助                                                         */
/* ================================================================ */

/* 构造「状态 OK」应答：命令字 + 单字节 0x00 */
static uint16_t build_status_ok(uint8_t *resp, uint16_t cap, uint32_t cmd)
{
    uint8_t d = 0x00U;
    return opm_frame_build(resp, cap, cmd, &d, 1U);
}

/* 通道号合法性：0(全部) 或 1..channel_count */
static bool channel_valid(uint8_t ch)
{
    return (ch == 0U) || (ch <= g_opm_dev.channel_count);
}

/* 读取某通道光功率(dBm)，来自 ADC_Task。
 *
 * 探测器测得的是电流(dBmA)，与光功率(dBm)之间是随波长变化的响应度换算：
 *   电流 = 响应度(λ) × 光功率  →  对数域退化为按波长减一个常数偏移量
 *   光功率(dBm) = 测量值(dBmA) − offset(当前工作波长)
 * offset 由出厂标定通过 WRPO 写入 g_opm_dev.offset_db[通道][标定波长下标]。
 * 这里用该通道当前工作波长 work_wl(nm) 在标定波长表 cal_wl[] 中查下标，
 * 找到则减去对应偏移量；未标定该波长则不减(返回原始 dBmA)。
 */
static float get_power_dbm(uint8_t ch_index)
{
    if (ch_index >= (uint8_t)ADC_CHANNEL_COUNT)
    {
        return 0.0f;
    }

    float raw = adc_ch[ch_index].dBmA;

    if (ch_index >= g_opm_dev.channel_count)
    {
        return raw;
    }

    uint16_t wl = g_opm_dev.work_wl[ch_index];
    for (uint8_t i = 0; i < g_opm_dev.cal_wl_count; i++)
    {
        if (g_opm_dev.cal_wl[i] == wl)
        {
            return raw - g_opm_dev.offset_db[ch_index][i];
        }
    }
    return raw;
}

/* ================================================================ */
/*  各命令 handler                                                   */
/*  约定：返回应答字节数；返回 0 由 dispatch 统一转成 ERR。          */
/* ================================================================ */

/* 1) RDPN 获取产品名称 */
static uint16_t h_rdpn(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    return opm_frame_build(resp, cap, OPM_CMD_RDPN,
                           (const uint8_t *)g_opm_dev.product_name, 6U);
}

/* 2) RDSN 获取序列号 */
static uint16_t h_rdsn(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    return opm_frame_build(resp, cap, OPM_CMD_RDSN,
                           (const uint8_t *)g_opm_dev.serial, 12U);
}

/* 3) RDVR 获取版本号 */
static uint16_t h_rdvr(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    return opm_frame_build(resp, cap, OPM_CMD_RDVR, g_opm_dev.version, 4U);
}

/* 4) RDMC 获取 MAC 地址 */
static uint16_t h_rdmc(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    return opm_frame_build(resp, cap, OPM_CMD_RDMC, g_opm_dev.mac, 6U);
}

/* 5) RDIP 获取 IP 地址 */
static uint16_t h_rdip(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    return opm_frame_build(resp, cap, OPM_CMD_RDIP, g_opm_dev.ip, 4U);
}

/* 6) WRIP 修改 IP 地址 */
static uint16_t h_wrip(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 4U)
    {
        return 0;
    }
    memcpy(g_opm_dev.ip, req->data, 4U);
    s_netcfg_pending = true; /* 通知任务层把新 IP 写入 CH9121 */
    return build_status_ok(resp, cap, OPM_CMD_WRIP);
}

/* 7) RDPT 获取网络端口 */
static uint16_t h_rdpt(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    uint8_t d[2];
    opm_wr_u16le(d, g_opm_dev.port);
    return opm_frame_build(resp, cap, OPM_CMD_RDPT, d, 2U);
}

/* 8) WRPT 修改网络端口 */
static uint16_t h_wrpt(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 2U)
    {
        return 0;
    }
    g_opm_dev.port = opm_rd_u16le(req->data);
    s_netcfg_pending = true; /* 通知任务层把新端口写入 CH9121 */
    return build_status_ok(resp, cap, OPM_CMD_WRPT);
}

/* 9) RDCC 获取通道个数 */
static uint16_t h_rdcc(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    return opm_frame_build(resp, cap, OPM_CMD_RDCC, &g_opm_dev.channel_count, 1U);
}

/* 10) RDTM 获取通道采样平均时间 */
static uint16_t h_rdtm(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 1U)
    {
        return 0;
    }
    uint8_t ch = req->data[0];
    if (ch == 0U || ch > g_opm_dev.channel_count)
    {
        return 0;
    }
    uint8_t d[5];
    d[0] = ch;
    opm_wr_u32le(&d[1], g_opm_dev.sample_us[ch - 1U]);
    return opm_frame_build(resp, cap, OPM_CMD_RDTM, d, 5U);
}

/* 11) STTM 设置通道采样平均时间 */
static uint16_t h_sttm(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 5U)
    {
        return 0;
    }
    uint8_t ch = req->data[0];
    uint32_t us = opm_rd_u32le(&req->data[1]);
    if (ch == 0U || ch > g_opm_dev.channel_count || us < 50U)
    {
        return 0; /* 解析错误 */
    }
    g_opm_dev.sample_us[ch - 1U] = us;

    /* 采样平均时间只改滑动均值数组长度 N(=时间/4ms)，4ms 窗口不变。
     * 下限 20ms、上限 2s，由 ADC_Unpack 内部钳制。 */
    if (ADC_Unpack_SetAverageTimeUs(us) != ADC_STATUS_OK)
    {
        return 0;
    }
    return build_status_ok(resp, cap, OPM_CMD_STTM);
}

/* 12) RDWC 获取标定波长个数 */
static uint16_t h_rdwc(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    return opm_frame_build(resp, cap, OPM_CMD_RDWC, &g_opm_dev.cal_wl_count, 1U);
}

/* 13) RDWL 获取标定波长列表 */
static uint16_t h_rdwl(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    uint8_t d[OPM_MAX_CAL_WL * 2U];
    uint16_t n = 0;
    for (uint8_t i = 0; i < g_opm_dev.cal_wl_count; i++)
    {
        opm_wr_u16le(&d[n], g_opm_dev.cal_wl[i]);
        n += 2U;
    }
    return opm_frame_build(resp, cap, OPM_CMD_RDWL, d, n);
}

/* 14) RDWW 获取当前工作波长 */
static uint16_t h_rdww(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 1U)
    {
        return 0;
    }
    uint8_t ch = req->data[0];
    if (!channel_valid(ch))
    {
        return 0;
    }
    uint8_t d[1U + OPM_MAX_CHANNELS * 2U];
    uint16_t n = 0;
    d[n++] = ch;
    if (ch == 0U)
    {
        for (uint8_t i = 0; i < g_opm_dev.channel_count; i++)
        {
            opm_wr_u16le(&d[n], g_opm_dev.work_wl[i]);
            n += 2U;
        }
    }
    else
    {
        opm_wr_u16le(&d[n], g_opm_dev.work_wl[ch - 1U]);
        n += 2U;
    }
    return opm_frame_build(resp, cap, OPM_CMD_RDWW, d, n);
}

/* 15) STWW 设置当前工作波长 */
static uint16_t h_stww(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 3U)
    {
        return 0;
    }
    uint8_t ch = req->data[0];
    uint16_t wl = opm_rd_u16le(&req->data[1]);
    if (!channel_valid(ch))
    {
        return 0;
    }
    if (ch == 0U)
    {
        for (uint8_t i = 0; i < g_opm_dev.channel_count; i++)
        {
            g_opm_dev.work_wl[i] = wl;
        }
    }
    else
    {
        g_opm_dev.work_wl[ch - 1U] = wl;
    }
    return build_status_ok(resp, cap, OPM_CMD_STWW);
}

/* 16) RDPR 获取当前光功率值 */
static uint16_t h_rdpr(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 2U)
    {
        return 0;
    }
    uint8_t ch = req->data[0];
    uint8_t sub = req->data[1]; /* 固定 01 */
    if (!channel_valid(ch))
    {
        return 0;
    }
    uint8_t d[2U + OPM_MAX_CHANNELS * 4U];
    uint16_t n = 0;
    d[n++] = ch;
    d[n++] = sub;
    if (ch == 0U)
    {
        for (uint8_t i = 0; i < g_opm_dev.channel_count; i++)
        {
            opm_wr_f32le(&d[n], get_power_dbm(i));
            n += 4U;
        }
    }
    else
    {
        opm_wr_f32le(&d[n], get_power_dbm((uint8_t)(ch - 1U)));
        n += 4U;
    }
    return opm_frame_build(resp, cap, OPM_CMD_RDPR, d, n);
}

/* 17) WRPO 写入标定波长光功率偏移量 */
static uint16_t h_wrpo(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    /* data: [通道号][01][指定波长号][偏移量...] */
    if (req->data_len < 3U)
    {
        return 0;
    }
    uint8_t ch = req->data[0];
    uint8_t wl_idx = req->data[2];
    if (ch == 0U || ch > g_opm_dev.channel_count)
    {
        return 0;
    }
    const uint8_t *off = &req->data[3];
    uint16_t off_len = (uint16_t)(req->data_len - 3U);

    if (wl_idx == 0U)
    {
        /* 全部标定波长：长度应为 cal_wl_count*4 */
        if (off_len != (uint16_t)(g_opm_dev.cal_wl_count * 4U))
        {
            return 0;
        }
        for (uint8_t i = 0; i < g_opm_dev.cal_wl_count; i++)
        {
            g_opm_dev.offset_db[ch - 1U][i] = opm_rd_f32le(&off[i * 4U]);
        }
    }
    else
    {
        if (wl_idx > g_opm_dev.cal_wl_count || off_len != 4U)
        {
            return 0;
        }
        g_opm_dev.offset_db[ch - 1U][wl_idx - 1U] = opm_rd_f32le(off);
    }
    return build_status_ok(resp, cap, OPM_CMD_WRPO);
}

/* 18) RDPO 获取标定波长光功率偏移量 */
static uint16_t h_rdpo(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 3U)
    {
        return 0;
    }
    uint8_t ch = req->data[0];
    uint8_t sub = req->data[1];
    uint8_t wl_idx = req->data[2];
    if (ch == 0U || ch > g_opm_dev.channel_count)
    {
        return 0;
    }
    if (wl_idx > g_opm_dev.cal_wl_count)
    {
        return 0;
    }

    uint8_t d[3U + OPM_MAX_CAL_WL * 4U];
    uint16_t n = 0;
    d[n++] = ch;
    d[n++] = sub;
    d[n++] = wl_idx;
    if (wl_idx == 0U)
    {
        for (uint8_t i = 0; i < g_opm_dev.cal_wl_count; i++)
        {
            opm_wr_f32le(&d[n], g_opm_dev.offset_db[ch - 1U][i]);
            n += 4U;
        }
    }
    else
    {
        opm_wr_f32le(&d[n], g_opm_dev.offset_db[ch - 1U][wl_idx - 1U]);
        n += 4U;
    }
    return opm_frame_build(resp, cap, OPM_CMD_RDPO, d, n);
}

/* 19) BOOT 重启命令 */
static uint16_t h_boot(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    s_reboot_pending = true; /* 任务发出应答后执行系统复位 */
    return build_status_ok(resp, cap, OPM_CMD_BOOT);
}

/* 20) STMP 启动光功率连续测量（高速版本）
 * 采样粒度固定 100ms。data: [count(4)][采样时间us(4)]。
 * 采样时间被解释为“总时长上限”：有效次数 = min(count, 时长/100ms, 上限 ADC_CAP_MAX_COUNT)。 */
static uint16_t h_stmp(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 8U)
    {
        return 0;
    }
    uint32_t count = opm_rd_u32le(&req->data[0]);
    uint32_t us = opm_rd_u32le(&req->data[4]);
    uint32_t max_by_time = us / 100000U; /* 100ms = 100000us 每点 */
    uint32_t eff;

    if (count < 1U)
    {
        return 0;
    }
    eff = count;
    if (max_by_time < eff)
    {
        eff = max_by_time;
    }
    if (eff > ADC_CAP_MAX_COUNT)
    {
        eff = ADC_CAP_MAX_COUNT;
    }
    if (eff == 0U)
    {
        return 0; /* 时长不足 100ms，采不到点 */
    }

    g_opm_dev.meas_mode = OPM_MEAS_CONTINUOUS;
    g_opm_dev.meas_target = eff;
    g_opm_dev.meas_sample_us = 100000U; /* 固定 100ms */
    g_opm_dev.meas_done = 0;
    adc_capture_start(eff);
    return build_status_ok(resp, cap, OPM_CMD_STMP);
}

/* 21) STMT 启动外部连续触发单次量测（高速版本）
 * 未实现：当前硬件未预留外部触发通道，返回解析错误。 */
static uint16_t h_stmt(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    (void)resp;
    (void)cap;
    return 0;
}

/* 22) STST 启动外部单次触发多次量测（高速版本）
 * 未实现：当前硬件未预留外部触发通道，返回解析错误。 */
static uint16_t h_stst(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    (void)resp;
    (void)cap;
    return 0;
}

/* 23) STSM 停止光功率连续量测（高速版本） */
static uint16_t h_stsm(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    adc_capture_stop();
    g_opm_dev.meas_mode = OPM_MEAS_NORMAL;
    return build_status_ok(resp, cap, OPM_CMD_STSM);
}

/* 24) RDFC 读取光功率连续测量完成次数（高速版本） */
static uint16_t h_rdfc(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    (void)req;
    uint8_t d[4];
    opm_wr_u32le(d, adc_capture_get_done());
    return opm_frame_build(resp, cap, OPM_CMD_RDFC, d, 4U);
}

/* 25) RDMR 读取光功率连续测量结果（高速版本）
 * data: [通道(1)][01(1)][起始点(4)][点数(4)]。按 start/len 从连续采样缓冲取历史 dBmA。
 * 一帧最多回传若干点(受帧上限约束)，上位机可分批读。 */
static uint16_t h_rdmr(const OpmFrame *req, uint8_t *resp, uint16_t cap)
{
    if (req->data_len != 10U)
    {
        return 0;
    }
    uint8_t ch = req->data[0];
    uint8_t sub = req->data[1];
    uint32_t start = opm_rd_u32le(&req->data[2]);
    uint32_t datalen = opm_rd_u32le(&req->data[6]);
    if (ch == 0U || ch > g_opm_dev.channel_count)
    {
        return 0;
    }

    /* 单帧上限：附加数据 = 2+4+4 + 4*datalen，需 <= cap 与本地组包缓冲 d[] 两者。 */
    uint16_t head = 2U + 4U + 4U;
    uint32_t max_by_cap = (cap > (OPM_OVERHEAD + head)) ? ((cap - OPM_OVERHEAD - head) / 4U) : 0U;
    uint32_t max_by_buf = (OPM_MAX_FRAME_LEN - head) / 4U;
    if (datalen > max_by_cap)
    {
        datalen = max_by_cap;
    }
    if (datalen > max_by_buf)
    {
        datalen = max_by_buf;
    }

    float pts[(OPM_MAX_FRAME_LEN - 10U) / 4U];
    uint32_t got = adc_capture_read((uint8_t)(ch - 1U), start, pts, datalen);

    uint8_t d[OPM_MAX_FRAME_LEN];
    uint16_t n = 0;
    d[n++] = ch;
    d[n++] = sub;
    opm_wr_u32le(&d[n], start);
    n += 4U;
    opm_wr_u32le(&d[n], got);
    n += 4U;
    for (uint32_t i = 0; i < got; i++)
    {
        opm_wr_f32le(&d[n], pts[i]);
        n += 4U;
    }
    return opm_frame_build(resp, cap, OPM_CMD_RDMR, d, n);
}

/* ================================================================ */
/*  分发                                                             */
/* ================================================================ */
uint16_t opm_dispatch(const OpmFrame *req, uint8_t *resp_buf, uint16_t resp_cap)
{
    uint16_t r = 0;

    switch (req->cmd)
    {
    case OPM_CMD_RDPN: r = h_rdpn(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDSN: r = h_rdsn(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDVR: r = h_rdvr(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDMC: r = h_rdmc(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDIP: r = h_rdip(req, resp_buf, resp_cap); break;
    case OPM_CMD_WRIP: r = h_wrip(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDPT: r = h_rdpt(req, resp_buf, resp_cap); break;
    case OPM_CMD_WRPT: r = h_wrpt(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDCC: r = h_rdcc(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDTM: r = h_rdtm(req, resp_buf, resp_cap); break;
    case OPM_CMD_STTM: r = h_sttm(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDWC: r = h_rdwc(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDWL: r = h_rdwl(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDWW: r = h_rdww(req, resp_buf, resp_cap); break;
    case OPM_CMD_STWW: r = h_stww(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDPR: r = h_rdpr(req, resp_buf, resp_cap); break;
    case OPM_CMD_WRPO: r = h_wrpo(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDPO: r = h_rdpo(req, resp_buf, resp_cap); break;
    case OPM_CMD_BOOT: r = h_boot(req, resp_buf, resp_cap); break;
    case OPM_CMD_STMP: r = h_stmp(req, resp_buf, resp_cap); break;
    case OPM_CMD_STMT: r = h_stmt(req, resp_buf, resp_cap); break;
    case OPM_CMD_STST: r = h_stst(req, resp_buf, resp_cap); break;
    case OPM_CMD_STSM: r = h_stsm(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDFC: r = h_rdfc(req, resp_buf, resp_cap); break;
    case OPM_CMD_RDMR: r = h_rdmr(req, resp_buf, resp_cap); break;
    default: r = 0; break; /* 未知命令 */
    }

    /* handler 返回 0（未知命令/参数非法）→ 统一回「命令解析错误」 */
    if (r == 0)
    {
        r = opm_build_error(resp_buf, resp_cap);
    }
    return r;
}
