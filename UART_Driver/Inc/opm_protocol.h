/**
 * @file    opm_protocol.h
 * @brief   多通道光功率计通信协议层（与物理串口无关）。
 *
 *  帧格式（上位机 <-> 下位机，本 MCU 为下位机/从机，被动应答）：
 *
 *    +--------+------------+-------------+----------+-----------+
 *    | 包头   | 包长度     | 命令字      | 附加数据 | 校验和    |
 *    | 1 字节 | 2 字节 LE  | 4 字节 ASCII| N 字节   | 1 字节    |
 *    | 0xAA   | =总长-3    | 如 "RDPN"   | 可空     | 见下      |
 *    +--------+------------+-------------+----------+-----------+
 *
 *  - 包长度 = 命令字(4) + 附加数据(N) + 校验和(1) = N + 5，等价于「整帧字节数 - 3」。
 *  - 校验和 = 除【校验和】外所有字节之和的低 8 位。
 *  - 多字节数值均为 Intel Little-Endian。
 *
 *  UART3(经 CH9121 透传网口) 与 UART5(经 TTL 转 232) 共用本层，
 *  仅物理收发通道不同；命令解析/分发/应答完全一致，回复回到请求来的那个口。
 */

#ifndef _OPM_PROTOCOL_H
#define _OPM_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ================================================================ */
    /*  基本常量                                                         */
    /* ================================================================ */
#define OPM_FRAME_HEADER 0xAAU     /* 固定包头                        */
#define OPM_HEADER_LEN 1U          /* 包头长度                        */
#define OPM_LENGTH_LEN 2U          /* 包长度字段长度                  */
#define OPM_CMDWORD_LEN 4U         /* 命令字长度                      */
#define OPM_CHECKSUM_LEN 1U        /* 校验和长度                      */
#define OPM_OVERHEAD (OPM_HEADER_LEN + OPM_LENGTH_LEN + OPM_CMDWORD_LEN + OPM_CHECKSUM_LEN) /* 8 */

#define OPM_MAX_CHANNELS 8U        /* 最大通道数                      */
#define OPM_MAX_CAL_WL 16U         /* 最大标定波长个数（手册列出 14） */

/* 单帧最大长度：连续测量结果读取一次最多 65522/4 个 float，实际受 RX 缓冲限制。
 * 这里给收发工作缓冲一个上限；连续读结果可分批。 */
#define OPM_MAX_FRAME_LEN 300U

    /* ================================================================ */
    /*  命令字（4 字节 ASCII 打包成 32 位，便于快速比较）                */
    /*  打包顺序与接收字节序一致：b0<<24 | b1<<16 | b2<<8 | b3           */
    /* ================================================================ */
#define OPM_CMD4(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

    /*  读类命令：R D x x                                                */
#define OPM_CMD_RDPN OPM_CMD4('R', 'D', 'P', 'N') /* 1  获取产品名称        */
#define OPM_CMD_RDSN OPM_CMD4('R', 'D', 'S', 'N') /* 2  获取序列号          */
#define OPM_CMD_RDVR OPM_CMD4('R', 'D', 'V', 'R') /* 3  获取版本号          */
#define OPM_CMD_RDMC OPM_CMD4('R', 'D', 'M', 'C') /* 4  获取 MAC 地址       */
#define OPM_CMD_RDIP OPM_CMD4('R', 'D', 'I', 'P') /* 5  获取 IP 地址        */
#define OPM_CMD_WRIP OPM_CMD4('W', 'R', 'I', 'P') /* 6  修改 IP 地址        */
#define OPM_CMD_RDPT OPM_CMD4('R', 'D', 'P', 'T') /* 7  获取网络端口        */
#define OPM_CMD_WRPT OPM_CMD4('W', 'R', 'P', 'T') /* 8  修改网络端口        */
#define OPM_CMD_RDCC OPM_CMD4('R', 'D', 'C', 'C') /* 9  获取通道个数        */
#define OPM_CMD_RDTM OPM_CMD4('R', 'D', 'T', 'M') /* 10 获取通道采样平均时间*/
#define OPM_CMD_STTM OPM_CMD4('S', 'T', 'T', 'M') /* 11 设置通道采样平均时间*/
#define OPM_CMD_RDWC OPM_CMD4('R', 'D', 'W', 'C') /* 12 获取标定波长个数    */
#define OPM_CMD_RDWL OPM_CMD4('R', 'D', 'W', 'L') /* 13 获取标定波长列表    */
#define OPM_CMD_RDWW OPM_CMD4('R', 'D', 'W', 'W') /* 14 获取当前工作波长    */
#define OPM_CMD_STWW OPM_CMD4('S', 'T', 'W', 'W') /* 15 设置当前工作波长    */
#define OPM_CMD_RDPR OPM_CMD4('R', 'D', 'P', 'R') /* 16 获取当前光功率值    */
#define OPM_CMD_WRPO OPM_CMD4('W', 'R', 'P', 'O') /* 17 写入标定波长偏移量  */
#define OPM_CMD_RDPO OPM_CMD4('R', 'D', 'P', 'O') /* 18 获取标定波长偏移量  */
#define OPM_CMD_BOOT OPM_CMD4('B', 'O', 'O', 'T') /* 19 重启命令            */
#define OPM_CMD_STMP OPM_CMD4('S', 'T', 'M', 'P') /* 20 启动连续测量        */
#define OPM_CMD_STMT OPM_CMD4('S', 'T', 'M', 'T') /* 21 启动外部连续触发单次*/
#define OPM_CMD_STST OPM_CMD4('S', 'T', 'S', 'T') /* 22 启动外部单次触发多次*/
#define OPM_CMD_STSM OPM_CMD4('S', 'T', 'S', 'M') /* 23 停止连续量测        */
#define OPM_CMD_RDFC OPM_CMD4('R', 'D', 'F', 'C') /* 24 读取连续测量完成次数*/
#define OPM_CMD_RDMR OPM_CMD4('R', 'D', 'M', 'R') /* 25 读取连续测量结果    */
    /* 26 命令解析错误：应答帧命令字为 3 字节 "ERR"，非接收命令，单独构造。 */

    /* ================================================================ */
    /*  解析出的请求帧视图                                               */
    /* ================================================================ */
    typedef struct
    {
        uint32_t cmd;         /* 打包后的 4 字节命令字               */
        const uint8_t *data;  /* 附加数据指针（指向源缓冲内部）      */
        uint16_t data_len;    /* 附加数据长度 N                      */
    } OpmFrame;

    /* ================================================================ */
    /*  连续测量（高速版本）运行状态                                     */
    /* ================================================================ */
    typedef enum
    {
        OPM_MEAS_NORMAL = 0,  /* 普通量测                            */
        OPM_MEAS_CONTINUOUS,  /* STMP 连续测量                       */
        OPM_MEAS_TRIG_SINGLE, /* STMT 外部连续触发单次               */
        OPM_MEAS_TRIG_MULTI,  /* STST 外部单次触发多次               */
    } OpmMeasMode;

    /* ================================================================ */
    /*  共享设备状态（两个口的 handler 共同读写，访问需加锁）            */
    /* ================================================================ */
    typedef struct
    {
        char product_name[6];   /* 如 "PM4177"                        */
        char serial[12];        /* 如 "PM2017071801"                  */
        uint8_t version[4];     /* 硬件主/副、软件主/副               */
        uint8_t mac[6];         /* MAC 地址                           */
        uint8_t ip[4];          /* IP 地址（如 10.0.0.10）            */
        uint16_t port;          /* 网络端口（如 8888）                */

        uint8_t channel_count;  /* 通道个数 1/2/4/8                   */

        uint8_t cal_wl_count;                /* 标定波长个数           */
        uint16_t cal_wl[OPM_MAX_CAL_WL];     /* 标定波长列表(nm)       */

        uint16_t work_wl[OPM_MAX_CHANNELS];  /* 各通道当前工作波长(nm) */
        uint32_t sample_us[OPM_MAX_CHANNELS];/* 各通道采样平均时间(us) */

        /* 各通道各标定波长的功率偏移量(dB) */
        float offset_db[OPM_MAX_CHANNELS][OPM_MAX_CAL_WL];

        /* 连续测量状态（高速版本） */
        OpmMeasMode meas_mode;
        uint32_t meas_target;    /* 目标测量/触发次数                 */
        uint32_t meas_done;      /* 已完成次数                        */
        uint32_t meas_sample_us; /* 连续测量采样时间                  */
    } OpmDeviceState;

    /* 全局设备状态实例（定义在 opm_protocol.c）。
     * 注意：并发访问由调用方(channel/task 层)用 mutex 保护。 */
    extern OpmDeviceState g_opm_dev;

    /* ================================================================ */
    /*  Little-Endian 读写辅助                                           */
    /* ================================================================ */
    static inline uint16_t opm_rd_u16le(const uint8_t *p)
    {
        return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    }
    static inline uint32_t opm_rd_u32le(const uint8_t *p)
    {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    static inline void opm_wr_u16le(uint8_t *p, uint16_t v)
    {
        p[0] = (uint8_t)(v & 0xFFU);
        p[1] = (uint8_t)((v >> 8) & 0xFFU);
    }
    static inline void opm_wr_u32le(uint8_t *p, uint32_t v)
    {
        p[0] = (uint8_t)(v & 0xFFU);
        p[1] = (uint8_t)((v >> 8) & 0xFFU);
        p[2] = (uint8_t)((v >> 16) & 0xFFU);
        p[3] = (uint8_t)((v >> 24) & 0xFFU);
    }
    static inline void opm_wr_f32le(uint8_t *p, float f)
    {
        uint32_t u;
        memcpy(&u, &f, sizeof(u));
        opm_wr_u32le(p, u);
    }
    static inline float opm_rd_f32le(const uint8_t *p)
    {
        uint32_t u = opm_rd_u32le(p);
        float f;
        memcpy(&f, &u, sizeof(f));
        return f;
    }

    /* ================================================================ */
    /*  协议 API                                                        */
    /* ================================================================ */

    /**
     * @brief 计算校验和：对 [buf, buf+len) 所有字节求和取低 8 位。
     */
    uint8_t opm_checksum(const uint8_t *buf, uint16_t len);

    /**
     * @brief 校验并解析一整帧。
     * @param buf  待解析缓冲（应指向以 0xAA 开头的一帧）
     * @param len  缓冲中可用字节数
     * @param out  解析结果（命令字/数据指针/数据长度）
     * @return 该帧总字节数(>0) 表示成功；<=0 表示失败：
     *         -1 数据不足（还需更多字节）
     *         -2 包头错误
     *         -3 校验和错误
     */
    int opm_frame_parse(const uint8_t *buf, uint16_t len, OpmFrame *out);

    /**
     * @brief 组装一帧到 out（自动填包头/包长度/校验和）。
     * @param out       输出缓冲（容量应 >= data_len + OPM_OVERHEAD）
     * @param cmd       4 字节命令字（用 OPM_CMD_xxx）
     * @param data      附加数据（可为 NULL）
     * @param data_len  附加数据长度
     * @return 整帧总字节数；0 表示容量不足/参数错误。
     */
    uint16_t opm_frame_build(uint8_t *out, uint16_t out_cap,
                             uint32_t cmd, const uint8_t *data, uint16_t data_len);

    /**
     * @brief 构造「命令解析错误」应答帧（AA 04 00 45 52 52 97）。
     * @return 帧长度（固定 7）。
     */
    uint16_t opm_build_error(uint8_t *out, uint16_t out_cap);

    /**
     * @brief 分发一条请求，产生应答字节。
     * @param req       已解析的请求帧
     * @param resp_buf  应答输出缓冲
     * @param resp_cap  应答缓冲容量
     * @return 应答字节数(>0)；0 表示无应答。
     * @note  设备状态并发保护由调用方负责。
     */
    uint16_t opm_dispatch(const OpmFrame *req, uint8_t *resp_buf, uint16_t resp_cap);

    /**
     * @brief 将某通道的原始 dBmA 按当前工作波长偏置换算为 dBm。
     *        标定波长端点使用端点偏置，端点之间线性插值，范围外返回原始 dBmA。
     */
    float opm_apply_power_offset_dbm(uint8_t ch_index, float dbma);

    /**
     * @brief 读取某通道当前光功率(dBm)，即 adc_ch[ch].dBmA 减当前波长偏置。
     */
    float opm_get_power_dbm(uint8_t ch_index);

    /**
     * @brief 初始化设备状态默认值（产品名/序列号/IP/端口/波长表等）。
     */
    void opm_device_state_init(void);

    /**
     * @brief 是否有重启请求挂起（BOOT 命令置位）。
     *        任务在把 BOOT 应答发出后，应查询本函数并执行系统复位。
     */
    bool opm_reboot_pending(void);

    /**
     * @brief 读取并清除「网络参数需下发 CH9121」标志（WRIP/WRPT 置位）。
     *        任务在把应答发出后，应查询本函数；为真则把 g_opm_dev 的
     *        IP/端口通过 ch9121_write_ip_port() 写入 CH9121 并复位生效。
     * @return true = 有待下发的网络参数（返回后标志自动清除）。
     */
    bool opm_netcfg_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* _OPM_PROTOCOL_H */
