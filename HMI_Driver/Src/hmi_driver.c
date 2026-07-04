/**
 * @file    hmi_driver.c
 * @brief   HMI protocol driver — command packing (all TX_ macros write to
 *          a static pack buffer), frame unpacking, and all HMI command
 *          function implementations.
 *
 *          The original command API is preserved; command functions pack into
 *          this file's local command buffer and send one DMA transfer per
 *          command through hmi_uart_send().
 */
#include "hmi_driver.h"
#include <string.h>

/* ================================================================ */
/*  Pack buffer (replaces old direct-UART send)                      */
/* ================================================================ */
#define HMI_PACK_BUF_SIZE 256U
static uint8_t hmi_pack_buf[HMI_PACK_BUF_SIZE];
static uint16_t hmi_pack_pos;
static bool hmi_pack_overflow;

/* ── CRC16 helpers (unchanged from original) ────────────────────── */
#if (CRC16_ENABLE)

static uint16 _crc16 = 0xffff;

static void AddCRC16(uint8 *buffer, uint16 n, uint16 *pcrc)
{
    uint16 i, j, carry_flag, a;
    for (i = 0; i < n; i++)
    {
        *pcrc = *pcrc ^ buffer[i];
        for (j = 0; j < 8; j++)
        {
            a = *pcrc;
            carry_flag = a & 0x0001;
            *pcrc = *pcrc >> 1;
            if (carry_flag == 1)
                *pcrc = *pcrc ^ 0xa001;
        }
    }
}

uint16 CheckCRC16(uint8 *buffer, uint16 n)
{
    uint16 crc0 = 0x0;
    uint16 crc1 = 0xffff;
    if (n >= 2)
    {
        crc0 = ((buffer[n - 2] << 8) | buffer[n - 1]);
        AddCRC16(buffer, n - 2, &crc1);
    }
    return (crc0 == crc1);
}

/* ── Pack-buffer write with CRC ─────────────────────────────────── */
#define SEND_DATA(c)                                     \
    do                                                   \
    {                                                    \
        AddCRC16(&(c), 1, &_crc16);                      \
        if (hmi_pack_pos < HMI_PACK_BUF_SIZE)            \
            hmi_pack_buf[hmi_pack_pos++] = (uint8_t)(c); \
        else                                             \
            hmi_pack_overflow = true;                    \
    } while (0)

#define BEGIN_CMD()                              \
    do                                           \
    {                                            \
        hmi_pack_pos = 0;                        \
        hmi_pack_overflow = false;               \
        _crc16 = 0xFFFF;                         \
        if (hmi_pack_pos < HMI_PACK_BUF_SIZE)    \
            hmi_pack_buf[hmi_pack_pos++] = 0xEE; \
    } while (0)

#define END_CMD()                                            \
    do                                                       \
    {                                                        \
        uint16_t __crc = _crc16;                             \
        TX_8((uint8_t)(__crc >> 8));                         \
        TX_8((uint8_t)(__crc & 0xFF));                       \
        TX_8(0xFF);                                          \
        TX_8(0xFC);                                          \
        TX_8(0xFF);                                          \
        TX_8(0xFF);                                          \
        if (!hmi_pack_overflow)                              \
            (void)hmi_uart_send(hmi_pack_buf, hmi_pack_pos); \
    } while (0)

#else /* NO CRC16 */

#define SEND_DATA(c)                                     \
    do                                                   \
    {                                                    \
        if (hmi_pack_pos < HMI_PACK_BUF_SIZE)            \
            hmi_pack_buf[hmi_pack_pos++] = (uint8_t)(c); \
        else                                             \
            hmi_pack_overflow = true;                    \
    } while (0)

#define BEGIN_CMD()                              \
    do                                           \
    {                                            \
        hmi_pack_pos = 0;                        \
        hmi_pack_overflow = false;               \
        if (hmi_pack_pos < HMI_PACK_BUF_SIZE)    \
            hmi_pack_buf[hmi_pack_pos++] = 0xEE; \
    } while (0)

#define END_CMD()                                            \
    do                                                       \
    {                                                        \
        TX_8(0xFF);                                          \
        TX_8(0xFC);                                          \
        TX_8(0xFF);                                          \
        TX_8(0xFF);                                          \
        if (!hmi_pack_overflow)                              \
            (void)hmi_uart_send(hmi_pack_buf, hmi_pack_pos); \
    } while (0)

#endif /* CRC16_ENABLE */

/* ── TX macros (write into pack buffer, same semantics as before) ── */
#define TX_8(P1) SEND_DATA((uint8_t)((P1)&0xFF))
#define TX_8N(P, N) SendNU8((uint8_t *)(P), (N))
#define TX_16(P1)                  \
    do                             \
    {                              \
        TX_8((uint16_t)(P1) >> 8); \
        TX_8((uint16_t)(P1)&0xFF); \
    } while (0)
#define TX_16N(P, N) SendNU16((uint16_t *)(P), (N))
#define TX_32(P1)                     \
    do                                \
    {                                 \
        TX_16((uint32_t)(P1) >> 16);  \
        TX_16((uint32_t)(P1)&0xFFFF); \
    } while (0)

/* ── Multi-byte helpers (write strings / arrays into pack buffer) ── */

void SendStrings(uchar *str)
{
    while (*str)
    {
        TX_8(*str);
        str++;
    }
}

void SendNU8(uint8 *pData, uint16 nDataLen)
{
    uint16 i = 0;
    for (; i < nDataLen; ++i)
    {
        TX_8(pData[i]);
    }
}

void SendNU16(uint16 *pData, uint16 nDataLen)
{
    uint16 i = 0;
    for (; i < nDataLen; ++i)
    {
        TX_16(pData[i]);
    }
}

/* ================================================================ */
/*  Command functions (bodies unchanged, TX_/BEGIN/END redefined)    */
/* ================================================================ */
/** HMI command. */
void SetHandShake()
{
    BEGIN_CMD();
    TX_8(0x04);
    END_CMD();
}

/** HMI command. */
void SetFcolor(uint16 color)
{
    BEGIN_CMD();
    TX_8(0x41);
    TX_16(color);
    END_CMD();
}
/** HMI command. */
void SetBcolor(uint16 color)
{
    BEGIN_CMD();
    TX_8(0x42);
    TX_16(color);
    END_CMD();
}
/** HMI command. */
void ColorPicker(uint8 mode, uint16 x, uint16 y)
{
    BEGIN_CMD();
    TX_8(0xA3);
    TX_8(mode);
    TX_16(x);
    TX_16(y);
    END_CMD();
}
/** HMI command. */
void GUI_CleanScreen()
{
    BEGIN_CMD();
    TX_8(0x01);
    END_CMD();
}
/** HMI command. */
void SetTextSpace(uint8 x_w, uint8 y_w)
{
    BEGIN_CMD();
    TX_8(0x43);
    TX_8(x_w);
    TX_8(y_w);
    END_CMD();
}
/** HMI command. */
void SetFont_Region(uint8 enable, uint16 width, uint16 height)
{
    BEGIN_CMD();
    TX_8(0x45);
    TX_8(enable);
    TX_16(width);
    TX_16(height);
    END_CMD();
}
/** HMI command. */
void SetFilterColor(uint16 fillcolor_dwon, uint16 fillcolor_up)
{
    BEGIN_CMD();
    TX_8(0x44);
    TX_16(fillcolor_dwon);
    TX_16(fillcolor_up);
    END_CMD();
}

/** HMI command. */
void DisText(uint16 x, uint16 y, uint8 back, uint8 font, uchar *strings)
{
    BEGIN_CMD();
    TX_8(0x20);
    TX_16(x);
    TX_16(y);
    TX_8(back);
    TX_8(font);
    SendStrings(strings);
    END_CMD();
}
/** HMI command. */
void DisCursor(uint8 enable, uint16 x, uint16 y, uint8 width, uint8 height)
{
    BEGIN_CMD();
    TX_8(0x21);
    TX_8(enable);
    TX_16(x);
    TX_16(y);
    TX_8(width);
    TX_8(height);
    END_CMD();
}
/** HMI command. */
void DisFull_Image(uint16 image_id, uint8 masken)
{
    BEGIN_CMD();
    TX_8(0x31);
    TX_16(image_id);
    TX_8(masken);
    END_CMD();
}
/** HMI command. */
void DisArea_Image(uint16 x, uint16 y, uint16 image_id, uint8 masken)
{
    BEGIN_CMD();
    TX_8(0x32);
    TX_16(x);
    TX_16(y);
    TX_16(image_id);
    TX_8(masken);
    END_CMD();
}
/** HMI command. */
void DisCut_Image(uint16 x, uint16 y, uint16 image_id, uint16 image_x, uint16 image_y, uint16 image_l, uint16 image_w, uint8 masken)
{
    BEGIN_CMD();
    TX_8(0x33);
    TX_16(x);
    TX_16(y);
    TX_16(image_id);
    TX_16(image_x);
    TX_16(image_y);
    TX_16(image_l);
    TX_16(image_w);
    TX_8(masken);
    END_CMD();
}
/** HMI command. */
void DisFlashImage(uint16 x, uint16 y, uint16 flashimage_id, uint8 enable, uint8 playnum)
{
    BEGIN_CMD();
    TX_8(0x80);
    TX_16(x);
    TX_16(y);
    TX_16(flashimage_id);
    TX_8(enable);
    TX_8(playnum);
    END_CMD();
}
/** HMI command. */
void GUI_Dot(uint16 x, uint16 y)
{
    BEGIN_CMD();
    TX_8(0x50);
    TX_16(x);
    TX_16(y);
    END_CMD();
}
/** HMI command. */
void GUI_Line(uint16 x0, uint16 y0, uint16 x1, uint16 y1)
{
    BEGIN_CMD();
    TX_8(0x51);
    TX_16(x0);
    TX_16(y0);
    TX_16(x1);
    TX_16(y1);
    END_CMD();
}

/** HMI command. */
void GUI_ConDots(uint8 mode, uint16 *dot, uint16 dot_cnt)
{
    BEGIN_CMD();
    TX_8(0x63);
    TX_8(mode);
    TX_16N(dot, dot_cnt * 2);
    END_CMD();
}

/** HMI command. */
void GUI_ConSpaceDots(uint16 x, uint16 x_space, uint16 *dot_y, uint16 dot_cnt)
{
    BEGIN_CMD();
    TX_8(0x59);
    TX_16(x);
    TX_16(x_space);
    TX_16N(dot_y, dot_cnt);
    END_CMD();
}
/** HMI command. */
void GUI_FcolorConOffsetDots(uint16 x, uint16 y, uint16 *dot_offset, uint16 dot_cnt)
{
    BEGIN_CMD();
    TX_8(0x75);
    TX_16(x);
    TX_16(y);
    TX_16N(dot_offset, dot_cnt);
    END_CMD();
}
/** HMI command. */
void GUI_BcolorConOffsetDots(uint16 x, uint16 y, uint8 *dot_offset, uint16 dot_cnt)
{
    BEGIN_CMD();
    TX_8(0x76);
    TX_16(x);
    TX_16(y);
    TX_8N(dot_offset, dot_cnt); /* fixed: was TX_16N with uint8* param */
    END_CMD();
}
/** HMI command. */
void SetPowerSaving(uint8 enable, uint8 bl_off_level, uint8 bl_on_level, uint8 bl_on_time)
{
    BEGIN_CMD();
    TX_8(0x77);
    TX_8(enable);
    TX_8(bl_off_level);
    TX_8(bl_on_level);
    TX_8(bl_on_time);
    END_CMD();
}
/** HMI command. */
void GUI_FcolorConDots(uint16 *dot, uint16 dot_cnt)
{
    BEGIN_CMD();
    TX_8(0x68);
    TX_16N(dot, dot_cnt * 2);
    END_CMD();
}
/** HMI command. */
void GUI_BcolorConDots(uint16 *dot, uint16 dot_cnt)
{
    BEGIN_CMD();
    TX_8(0x69);
    TX_16N(dot, dot_cnt * 2);
    END_CMD();
}
/** HMI command. */
void GUI_Circle(uint16 x, uint16 y, uint16 r)
{
    BEGIN_CMD();
    TX_8(0x52);
    TX_16(x);
    TX_16(y);
    TX_16(r);
    END_CMD();
}
/** HMI command. */
void GUI_CircleFill(uint16 x, uint16 y, uint16 r)
{
    BEGIN_CMD();
    TX_8(0x53);
    TX_16(x);
    TX_16(y);
    TX_16(r);
    END_CMD();
}
/** HMI command. */
void GUI_Arc(uint16 x, uint16 y, uint16 r, uint16 sa, uint16 ea)
{
    BEGIN_CMD();
    TX_8(0x67);
    TX_16(x);
    TX_16(y);
    TX_16(r);
    TX_16(sa);
    TX_16(ea);
    END_CMD();
}
/** HMI command. */
void GUI_Rectangle(uint16 x0, uint16 y0, uint16 x1, uint16 y1)
{
    BEGIN_CMD();
    TX_8(0x54);
    TX_16(x0);
    TX_16(y0);
    TX_16(x1);
    TX_16(y1);
    END_CMD();
}
/** HMI command. */
void GUI_RectangleFill(uint16 x0, uint16 y0, uint16 x1, uint16 y1)
{
    BEGIN_CMD();
    TX_8(0x55);
    TX_16(x0);
    TX_16(y0);
    TX_16(x1);
    TX_16(y1);
    END_CMD();
}
/** HMI command. */
void GUI_Ellipse(uint16 x0, uint16 y0, uint16 x1, uint16 y1)
{
    BEGIN_CMD();
    TX_8(0x56);
    TX_16(x0);
    TX_16(y0);
    TX_16(x1);
    TX_16(y1);
    END_CMD();
}
/** HMI command. */
void GUI_EllipseFill(uint16 x0, uint16 y0, uint16 x1, uint16 y1)
{
    BEGIN_CMD();
    TX_8(0x57);
    TX_16(x0);
    TX_16(y0);
    TX_16(x1);
    TX_16(y1);
    END_CMD();
}
/** HMI command. */
void SetBackLight(uint8 light_level)
{
    BEGIN_CMD();
    TX_8(0x60);
    TX_8(light_level);
    END_CMD();
}

/** HMI command. */
void SetBuzzer(uint8 time)
{
    BEGIN_CMD();
    TX_8(0x61);
    TX_8(time);
    END_CMD();
}

void GUI_AreaInycolor(uint16 x0, uint16 y0, uint16 x1, uint16 y1)
{
    BEGIN_CMD();
    TX_8(0x65);
    TX_16(x0);
    TX_16(y0);
    TX_16(x1);
    TX_16(y1);
    END_CMD();
}
/** HMI command. */
void SetTouchPaneOption(uint8 enbale, uint8 beep_on, uint8 work_mode, uint8 press_calibration)
{
    uint8 options = 0;

    if (enbale)
        options |= 0x01;
    if (beep_on)
        options |= 0x02;
    if (work_mode)
        options |= (work_mode << 2);
    if (press_calibration)
        options |= (press_calibration << 5);

    BEGIN_CMD();
    TX_8(0x70);
    TX_8(options);
    END_CMD();
}
/** HMI command. */
void CalibrateTouchPane()
{
    BEGIN_CMD();
    TX_8(0x72);
    END_CMD();
}
/** HMI command. */
void TestTouchPane()
{
    BEGIN_CMD();
    TX_8(0x73);
    END_CMD();
}

/** HMI command. */
void LockDeviceConfig(void)
{
    BEGIN_CMD();
    TX_8(0x09);
    TX_8(0xDE);
    TX_8(0xED);
    TX_8(0x13);
    TX_8(0x31);
    END_CMD();
}

/** HMI command. */
void UnlockDeviceConfig(void)
{
    BEGIN_CMD();
    TX_8(0x08);
    TX_8(0xA5);
    TX_8(0x5A);
    TX_8(0x5F);
    TX_8(0xF5);
    END_CMD();
}
/** HMI command. */
void SetCommBps(uint8 option)
{
    BEGIN_CMD();
    TX_8(0xA0);
    TX_8(option);
    END_CMD();
}
/** HMI command. */
void WriteLayer(uint8 layer)
{
    BEGIN_CMD();
    TX_8(0xA1);
    TX_8(layer);
    END_CMD();
}
/** HMI command. */
void DisplyLayer(uint8 layer)
{
    BEGIN_CMD();
    TX_8(0xA2);
    TX_8(layer);
    END_CMD();
}
/** HMI command. */
void CopyLayer(uint8 src_layer, uint8 dest_layer)
{
    BEGIN_CMD();
    TX_8(0xA4);
    TX_8(src_layer);
    TX_8(dest_layer);
    END_CMD();
}
/** HMI command. */
void ClearLayer(uint8 layer)
{
    BEGIN_CMD();
    TX_8(0x05);
    TX_8(layer);
    END_CMD();
}

void GUI_DispRTC(uint8 enable, uint8 mode, uint8 font, uint16 color, uint16 x, uint16 y)
{
    BEGIN_CMD();
    TX_8(0x85);
    TX_8(enable);
    TX_8(mode);
    TX_8(font);
    TX_16(color);
    TX_16(x);
    TX_16(y);
    END_CMD();
}
/** HMI command. */
void WriteUserFlash(uint32 startAddress, uint16 length, uint8 *_data)
{
    BEGIN_CMD();
    TX_8(0x87);
    TX_32(startAddress);
    TX_8N(_data, length);
    END_CMD();
}
/** HMI command. */
void ReadUserFlash(uint32 startAddress, uint16 length)
{
    BEGIN_CMD();
    TX_8(0x88);
    TX_32(startAddress);
    TX_16(length);
    END_CMD();
}
/** HMI command. */
void GetScreen(uint16 screen_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x01);
    END_CMD();
}
/** HMI command. */
void SetScreen(uint16 screen_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x00);
    TX_16(screen_id);
    END_CMD();
}
/** HMI command. */
void SetScreenUpdateEnable(uint8 enable)
{
    BEGIN_CMD();
    TX_8(0xB3);
    TX_8(enable);
    END_CMD();
}
/** HMI command. */
void SetControlFocus(uint16 screen_id, uint16 control_id, uint8 focus)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x02);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(focus);
    END_CMD();
}
/** HMI command. */
void SetControlVisiable(uint16 screen_id, uint16 control_id, uint8 visible)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x03);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(visible);
    END_CMD();
}
/** HMI command. */
void SetControlEnable(uint16 screen_id, uint16 control_id, uint8 enable)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x04);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(enable);
    END_CMD();
}
/** HMI command. */
void SetButtonValue(uint16 screen_id, uint16 control_id, uchar state)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x10);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(state);
    END_CMD();
}
/** HMI command. */
void SetTextValue(uint16 screen_id, uint16 control_id, uchar *str)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x10);
    TX_16(screen_id);
    TX_16(control_id);
    SendStrings(str);
    END_CMD();
}

#if FIRMWARE_VER >= 908
/** HMI command. */
void SetTextInt32(uint16 screen_id, uint16 control_id, uint32 value, uint8 sign, uint8 fill_zero)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x07);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(sign ? 0X01 : 0X00);
    TX_8((fill_zero & 0x0f) | 0x80);
    TX_32(value);
    END_CMD();
}
/** HMI command. */
void SetTextFloat(uint16 screen_id, uint16 control_id, float value, uint8 precision, uint8 show_zeros)
{
    uint8 i = 0;

    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x07);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(0x02);
    TX_8((precision & 0x0f) | (show_zeros ? 0x80 : 0x00));

    for (i = 0; i < 4; ++i)
    {
        // ��Ҫ���ִ�С��
#if (0)
        TX_8(((uint8 *)&value)[i]);
#else
        TX_8(((uint8 *)&value)[3 - i]);
#endif
    }
    END_CMD();
}
#endif
/** HMI command. */
void SetProgressValue(uint16 screen_id, uint16 control_id, uint32 value)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x10);
    TX_16(screen_id);
    TX_16(control_id);
    TX_32(value);
    END_CMD();
}
/** HMI command. */
void SetMeterValue(uint16 screen_id, uint16 control_id, uint32 value)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x10);
    TX_16(screen_id);
    TX_16(control_id);
    TX_32(value);
    END_CMD();
}
/** HMI command. */
void Set_picMeterValue(uint16 screen_id, uint16 control_id, uint16 value)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x10);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16(value);
    END_CMD();
}
/** HMI command. */

void SetSliderValue(uint16 screen_id, uint16 control_id, uint32 value)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x10);
    TX_16(screen_id);
    TX_16(control_id);
    TX_32(value);
    END_CMD();
}
/** HMI command. */
void SetSelectorValue(uint16 screen_id, uint16 control_id, uint8 item)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x10);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(item);
    END_CMD();
}
/** HMI command. */
void GetControlValue(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x11);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}

/** HMI command. */
void AnimationStart(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x20);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}

/** HMI command. */
void AnimationStop(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x21);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void AnimationPause(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x22);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void AnimationPlayFrame(uint16 screen_id, uint16 control_id, uint8 frame_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x23);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(frame_id);
    END_CMD();
}
/** HMI command. */
void AnimationPlayPrev(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x24);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void AnimationPlayNext(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x25);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void GraphChannelAdd(uint16 screen_id, uint16 control_id, uint8 channel, uint16 color)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x30);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(channel);
    TX_16(color);
    END_CMD();
}
/** HMI command. */
void GraphChannelDel(uint16 screen_id, uint16 control_id, uint8 channel)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x31);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(channel);
    END_CMD();
}
/** HMI command. */
void GraphChannelDataAdd(uint16 screen_id, uint16 control_id, uint8 channel, uint8 *pData, uint16 nDataLen)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x32);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(channel);
    TX_16(nDataLen);
    TX_8N(pData, nDataLen);
    END_CMD();
}
/** HMI command. */
void GraphChannelDataClear(uint16 screen_id, uint16 control_id, uint8 channel)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x33);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(channel);
    END_CMD();
}
/** HMI command. */
void GraphSetViewport(uint16 screen_id, uint16 control_id, int16 x_offset, uint16 x_mul, int16 y_offset, uint16 y_mul)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x34);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16(x_offset);
    TX_16(x_mul);
    TX_16(y_offset);
    TX_16(y_mul);
    END_CMD();
}
/** HMI command. */
void BatchBegin(uint16 screen_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x12);
    TX_16(screen_id);
}
/** HMI command. */
void BatchSetButtonValue(uint16 control_id, uint8 state)
{
    TX_16(control_id);
    TX_16(1);
    TX_8(state);
}
/** HMI command. */
void BatchSetProgressValue(uint16 control_id, uint32 value)
{
    TX_16(control_id);
    TX_16(4);
    TX_32(value);
}

/** HMI command. */
void BatchSetSliderValue(uint16 control_id, uint32 value)
{
    TX_16(control_id);
    TX_16(4);
    TX_32(value);
}
/** HMI command. */
void BatchSetMeterValue(uint16 control_id, uint32 value)
{
    TX_16(control_id);
    TX_16(4);
    TX_32(value);
}
/** HMI command. */
uint32 GetStringLen(uchar *str)
{
    uchar *p = str;
    while (*str)
    {
        str++;
    }

    return (str - p);
}
/** HMI command. */
void BatchSetText(uint16 control_id, uchar *strings)
{
    TX_16(control_id);
    TX_16(GetStringLen(strings));
    SendStrings(strings);
}
/** HMI command. */
void BatchSetFrame(uint16 control_id, uint16 frame_id)
{
    TX_16(control_id);
    TX_16(2);
    TX_16(frame_id);
}

#if FIRMWARE_VER >= 908

/** HMI command. */
void BatchSetVisible(uint16 control_id, uint8 visible)
{
    TX_16(control_id);
    TX_8(1);
    TX_8(visible);
}
/** HMI command. */
void BatchSetEnable(uint16 control_id, uint8 enable)
{
    TX_16(control_id);
    TX_8(2);
    TX_8(enable);
}

#endif
/** HMI command. */
void BatchEnd()
{
    END_CMD();
}
/** HMI command. */
void SeTimer(uint16 screen_id, uint16 control_id, uint32 timeout)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x40);
    TX_16(screen_id);
    TX_16(control_id);
    TX_32(timeout);
    END_CMD();
}
/** HMI command. */
void StartTimer(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x41);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void StopTimer(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x42);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void PauseTimer(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x44);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void SetControlBackColor(uint16 screen_id, uint16 control_id, uint16 color)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x18);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16(color);
    END_CMD();
}
/** HMI command. */
void SetControlForeColor(uint16 screen_id, uint16 control_id, uint16 color)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x19);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16(color);
    END_CMD();
}
/** HMI command. */
void ShowPopupMenu(uint16 screen_id, uint16 control_id, uint8 show, uint16 focus_control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x13);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(show);
    TX_16(focus_control_id);
    END_CMD();
}
/** HMI command. */
void ShowKeyboard(uint8 show, uint16 x, uint16 y, uint8 type, uint8 option, uint8 max_len)
{
    BEGIN_CMD();
    TX_8(0x86);
    TX_8(show);
    TX_16(x);
    TX_16(y);
    TX_8(type);
    TX_8(option);
    TX_8(max_len);
    END_CMD();
}

/** HMI command — hide system keyboard. */
void HideKeyboard(void)
{
    BEGIN_CMD();
    TX_8(0x86);
    TX_8(0x00);
    END_CMD();
}

/** HMI command — set text control blink. */
void SetTextBlink(uint16 screen_id, uint16 control_id, uint16 cycle)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x15);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16(cycle);
    END_CMD();
}

#if FIRMWARE_VER >= 921
/** HMI command. */
void SetLanguage(uint8 ui_lang, uint8 sys_lang)
{
    uint8 lang = ui_lang;
    if (sys_lang)
        lang |= 0x80;

    BEGIN_CMD();
    TX_8(0xC1);
    TX_8(lang);
    TX_8(0xC1 + lang); // У�飬��ֹ�����޸�����
    END_CMD();
}
#endif

#if FIRMWARE_VER >= 921
/** HMI command. */
void FlashBeginSaveControl(uint32 version, uint32 address)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0xAA);
    TX_32(version);
    TX_32(address);
}

/** HMI command. */
void FlashSaveControl(uint16 screen_id, uint16 control_id)
{
    TX_16(screen_id);
    TX_16(control_id);
}
/** HMI command. */
void FlashEndSaveControl()
{
    END_CMD();
}
/** HMI command. */
void FlashRestoreControl(uint32 version, uint32 address)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0xAB);
    TX_32(version);
    TX_32(address);
    END_CMD();
}

#endif

#if FIRMWARE_VER >= 921
/** HMI command. */
void HistoryGraph_SetValueInt8(uint16 screen_id, uint16 control_id, uint8 *value, uint8 channel)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x60);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8N(value, channel);
    END_CMD();
}
/** HMI command. */
void HistoryGraph_SetValueInt16(uint16 screen_id, uint16 control_id, uint16 *value, uint8 channel)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x60);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16N(value, channel);
    END_CMD();
}
/** HMI command. */
void HistoryGraph_SetValueInt32(uint16 screen_id, uint16 control_id, uint32 *value, uint8 channel)
{
    uint8 i = 0;

    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x60);
    TX_16(screen_id);
    TX_16(control_id);

    for (; i < channel; ++i)
    {
        TX_32(value[i]);
    }

    END_CMD();
}
/** HMI command. */
void HistoryGraph_SetValueFloat(uint16 screen_id, uint16 control_id, float *value, uint8 channel)
{
    uint8 i = 0;
    uint32 tmp = 0;

    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x60);
    TX_16(screen_id);
    TX_16(control_id);

    for (; i < channel; ++i)
    {
        tmp = *(uint32 *)(value + i);
        TX_32(tmp);
    }

    END_CMD();
}
/** HMI command. */
void HistoryGraph_EnableSampling(uint16 screen_id, uint16 control_id, uint8 enable)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x61);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(enable);
    END_CMD();
}
/** HMI command. */
void HistoryGraph_ShowChannel(uint16 screen_id, uint16 control_id, uint8 channel, uint8 show)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x62);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(channel);
    TX_8(show);
    END_CMD();
}
/** HMI command. */
void HistoryGraph_SetTimeLength(uint16 screen_id, uint16 control_id, uint16 sample_count)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x63);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(0x00);
    TX_16(sample_count);
    END_CMD();
}

/** HMI command. */
void HistoryGraph_SetTimeFullScreen(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x63);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(0x01);
    END_CMD();
}
/** HMI command. */
void HistoryGraph_SetTimeZoom(uint16 screen_id, uint16 control_id, uint16 zoom, uint16 max_zoom, uint16 min_zoom)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x63);
    TX_16(screen_id);
    TX_16(control_id);
    TX_8(0x02);
    TX_16(zoom);
    TX_16(max_zoom);
    TX_16(min_zoom);
    END_CMD();
}

#endif

#if SD_FILE_EN
/** HMI command. */
void SD_IsInsert(void)
{
    BEGIN_CMD();
    TX_8(0x36);
    TX_8(0x01);
    END_CMD();
}
/** HMI command. */
void SD_CreateFile(uint8 *filename, uint8 mode)
{
    BEGIN_CMD();
    TX_8(0x36);
    TX_8(0x05);
    TX_8(mode);
    SendStrings(filename);
    END_CMD();
}
/** HMI command. */
void SD_CreateFileByTime(uint8 *ext)
{
    BEGIN_CMD();
    TX_8(0x36);
    TX_8(0x02);
    SendStrings(ext);
    END_CMD();
}
/** HMI command. */
void SD_WriteFile(uint8 *buffer, uint16 dlc)
{
    BEGIN_CMD();
    TX_8(0x36);
    TX_8(0x03);
    TX_16(dlc);
    TX_8N(buffer, dlc);
    END_CMD();
}
/** HMI command. */
void SD_ReadFile(uint32 offset, uint16 dlc)
{
    BEGIN_CMD();
    TX_8(0x36);
    TX_8(0x07);
    TX_32(offset);
    TX_16(dlc);
    END_CMD();
}

/** HMI command. */
void SD_GetFileSize()
{
    BEGIN_CMD();
    TX_8(0x36);
    TX_8(0x06);
    END_CMD();
}
/** HMI command. */
void SD_CloseFile()
{
    BEGIN_CMD();
    TX_8(0x36);
    TX_8(0x04);
    END_CMD();
}

#endif // SD_FILE_EN
/** HMI command. */
void Record_SetEvent(uint16 screen_id, uint16 control_id, uint16 value, uint8 *time)
{
    uint8 i = 0;

    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x50);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16(value);

    if (time)
    {
        for (i = 0; i < 7; ++i)
            TX_8(time[i]);
    }

    END_CMD();
}
/** HMI command. */
void Record_ResetEvent(uint16 screen_id, uint16 control_id, uint16 value, uint8 *time)
{
    uint8 i = 0;

    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x51);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16(value);

    if (time)
    {
        for (i = 0; i < 7; ++i)
            TX_8(time[i]);
    }

    END_CMD();
}
/** HMI command. */
void Record_Add(uint16 screen_id, uint16 control_id, uint8 *record)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x52);
    TX_16(screen_id);
    TX_16(control_id);

    SendStrings(record);

    END_CMD();
}
/** HMI command. */
void Record_Clear(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x53);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void Record_SetOffset(uint16 screen_id, uint16 control_id, uint16 offset)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x54);
    TX_16(screen_id);
    TX_16(control_id);
    TX_16(offset);
    END_CMD();
}
/** HMI command. */
void Record_GetCount(uint16 screen_id, uint16 control_id)
{
    BEGIN_CMD();
    TX_8(0xB1);
    TX_8(0x55);
    TX_16(screen_id);
    TX_16(control_id);
    END_CMD();
}
/** HMI command. */
void ReadRTC(void)
{
    BEGIN_CMD();
    TX_8(0x82);
    END_CMD();
}

/** HMI command. */
void PlayMusic(uint8 *buffer)
{
    uint8 i = 0;

    BEGIN_CMD();
    if (buffer)
    {
        for (i = 0; i < 19; ++i)
            TX_8(buffer[i]);
    }
    END_CMD();
}

/* ================================================================ */
/*  pack_cmd — pack raw payload into frame and send                  */
/* ================================================================ */

/**
 * Pack raw command payload into HMI frame format and send.
 * Adds 0xEE header and 0xFFFCFFFF tail.  CRC is appended by the
 * BEGIN_CMD/END_CMD macros (not here).
 */
void hmi_driver_pack_cmd(const uint8_t *data, uint16_t len)
{
    uint16_t total = len + 5U; /* 1(header) + 4(tail) */
    if (total > HMI_PACK_BUF_SIZE)
        return;

    hmi_pack_pos = 0;
    hmi_pack_buf[hmi_pack_pos++] = 0xEE;

    for (uint16_t i = 0; i < len; i++)
    {
        hmi_pack_buf[hmi_pack_pos++] = data[i];
    }

    hmi_pack_buf[hmi_pack_pos++] = 0xFF;
    hmi_pack_buf[hmi_pack_pos++] = 0xFC;
    hmi_pack_buf[hmi_pack_pos++] = 0xFF;
    hmi_pack_buf[hmi_pack_pos++] = 0xFF;

    (void)hmi_uart_send(hmi_pack_buf, hmi_pack_pos);
}

/* ================================================================ */
/*  Frame unpacking — protocol-aware, per-control-type parsing       */
/* ================================================================ */

/* Maximum bytes allowed without seeing a frame header before reset */
#define HMI_FRAME_TIMEOUT_BYTES 512U

/* Flag set by hmi_driver_reset_frame(), consumed by hmi_driver_unpack() */
static volatile bool g_hmi_force_reset = false;

void hmi_driver_reset_frame(void)
{
    g_hmi_force_reset = true;
}

/* ── Big-endian helpers (avoid unaligned access on Cortex-M4) ───── */
static inline uint16_t be16_to_host(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static inline uint32_t be32_to_host(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/**
 * Unpack frames from the RX ring buffer.
 *
 * Protocol (V5.1):
 *   Frame:  0xEE [cmd] [msg] [payload...] 0xFF 0xFC 0xFF 0xFF
 *
 *   B1 0x01 — Screen change:      EE B1 01 [screen_id:2] FF FC FF FF
 *   B1 0x11 — Control notify:     EE B1 11 [scr:2] [ctrl:2] [type:1] [param...] FF FC FF FF
 *      Button (0x10):    param = [value:1] [state:1]
 *      Progress (0x12):  param = [value:4]  (big-endian uint32)
 *      Slider  (0x13):   param = [value:4]  (big-endian uint32)
 *      Meter   (0x14):   param = [value:4]  (big-endian uint32)
 *      Text    (0x11):   param = [len:?] [string:N]  (variable)
 *   0x01 / 0x03 — Touch:          EE 01/03 [x:2] [y:2] FF FC FF FF
 *   0xF7 — RTC:                   EE F7 [data:7] FF FC FF FF
 */
void hmi_driver_unpack(HmiRxRingBuf *rxbuf)
{
    static uint8_t frame_buf[CMD_MAX_SIZE];
    static uint16_t frame_pos = 0;
    static uint32_t tail_state = 0;  /* sliding 4-byte tail window  */
    static uint16_t noise_bytes = 0; /* bytes since last valid sync */

    while (hmi_rxbuf_available(rxbuf) > 0)
    {
        /* ── External reset request ──────────────────────────────── */
        if (g_hmi_force_reset)
        {
            frame_pos = 0;
            tail_state = 0;
            noise_bytes = 0;
            g_hmi_force_reset = false;
        }

        uint8_t byte;
        if (!hmi_rxbuf_pop(rxbuf, &byte))
            break;

        /* ── Frame timeout: too many bytes without a valid frame ─── */
        noise_bytes++;
        if (noise_bytes > HMI_FRAME_TIMEOUT_BYTES)
        {
            frame_pos = 0;
            tail_state = 0;
            noise_bytes = 0;
        }

        /* ── Sync to frame header ────────────────────────────────── */
        if (frame_pos == 0)
        {
            if (byte != 0xEE)
                continue; /* wait for next header */
            /* found header — reset noise counter */
            noise_bytes = 0;
        }

        /* ── Store byte into frame buffer ────────────────────────── */
        if (frame_pos < CMD_MAX_SIZE)
        {
            frame_buf[frame_pos++] = byte;
        }
        else
        {
            /* frame overflow — discard and resync */
            frame_pos = 0;
            tail_state = 0;
            continue;
        }

        /* ── Sliding 4-byte tail detection ───────────────────────── */
        tail_state = ((tail_state << 8) | byte) & 0xFFFFFFFFUL;

        if (tail_state != 0xFFFCFFFFUL)
            continue; /* not yet a complete frame */

        /* ============================================================ */
        /*  Complete frame received — parse it                           */
        /* ============================================================ */
        uint16_t frm_len = frame_pos;
        uint8_t cmd_type = 0;

        /* Minimum valid frame: 1(EE) + 1(cmd) + 4(tail) = 6 bytes */
        if (frm_len < 6)
            goto reset_frame;

        cmd_type = frame_buf[1];
        HmiEvent evt;
        memset(&evt, 0, sizeof(evt));

        switch (cmd_type)
        {
        /* ── B1: Control operation ────────────────────────────── */
        case 0xB1:
        {
            if (frm_len < 7)
                goto reset_frame; /* need at least EE B1 xx + tail */

            uint8_t ctrl_msg = frame_buf[2];

            if (ctrl_msg == 0x01)
            {
                /* ── Screen change notification ───────────────── */
                /*     EE B1 01 [screen_id:2] FF FC FF FF          */
                if (frm_len >= 8)
                {
                    evt.type = HMI_EVENT_SCREEN_CHANGE;
                    evt.screen_id = be16_to_host(&frame_buf[3]);
                    /* control_id / ctrl_type / value stay 0 */
                }
            }
            else if (ctrl_msg == 0x11)
            {
                /* ── Control notification ─────────────────────── */
                /*     EE B1 11 [scr:2] [ctrl:2] [type:1] [...]    */
                if (frm_len < 12) /* minimum: header(1)+B1+11+scr(2)+ctrl(2)+type(1)+tail(4) */
                    goto reset_frame;

                evt.type = HMI_EVENT_CONTROL_NOTIFY;
                evt.screen_id = be16_to_host(&frame_buf[3]);
                evt.control_id = be16_to_host(&frame_buf[5]);
                evt.ctrl_type = frame_buf[7];

                /* ── Extract value / state per control type ───── */
                uint16_t data_off = 8;                      /* param starts here */
                uint16_t data_len = frm_len - 4 - data_off; /* payload minus tail */

                switch (evt.ctrl_type)
                {
                case 0x10: /* Button — 1B value + 1B state */
                    if (data_len >= 1)
                        evt.value = frame_buf[data_off];
                    if (data_len >= 2)
                        evt.state = frame_buf[data_off + 1];
                    break;

                case 0x12: /* Progress — 4B big-endian value */
                case 0x13: /* Slider   — 4B big-endian value */
                case 0x14: /* Meter    — 4B big-endian value */
                    if (data_len >= 4)
                        evt.value = be32_to_host(&frame_buf[data_off]);
                    break;

                case 0x11: /* Text — variable-length string */
                    if (data_len > 0)
                    {
                        evt.param_len = (data_len < sizeof(evt.param)) ? (uint8_t)data_len : (uint8_t)sizeof(evt.param);
                        memcpy(evt.param, &frame_buf[data_off], evt.param_len);
                    }
                    break;

                default:
                    /* unknown control type — try 4B value if enough data */
                    if (data_len >= 4)
                        evt.value = be32_to_host(&frame_buf[data_off]);
                    else if (data_len >= 1)
                        evt.value = frame_buf[data_off];
                    break;
                }
            }
            /* else: other ctrl_msg values — silently ignored */
        }
        break;

        /* ── Touch events 0x01(press) / 0x03(release) ────────── */
        case 0x01:
        case 0x03:
            /* EE 01/03 [x:2] [y:2] FF FC FF FF   — minimum 8 bytes */
            if (frm_len >= 8)
            {
                evt.type = HMI_EVENT_TOUCH;
                evt.state = (cmd_type == 0x01) ? 1U : 0U;
                evt.x = be16_to_host(&frame_buf[2]);
                evt.y = be16_to_host(&frame_buf[4]);
            }
            break;

        /* ── Keyboard input 0x86 ─────────────────────────────── */
        case 0x86:
            /* EE 86 01 [strings...] FF FC FF FF
             * strings = user-entered ASCII, no length prefix */
            if (frm_len >= 6)
            {
                evt.type = HMI_EVENT_CONTROL_NOTIFY;
                evt.ctrl_type = 0x86; /* keyboard notification */
                evt.screen_id = 0;
                evt.control_id = 0;
                evt.param_len = (uint8_t)((frm_len - 6 > 31) ? 31 : (frm_len - 6));
                if (evt.param_len > 0)
                    memcpy(evt.param, &frame_buf[3], evt.param_len);
            }
            break;

        /* ── RTC response 0xF7 ────────────────────────────────── */
        case 0xF7:
            /* EE F7 [7 bytes BCD] FF FC FF FF  — minimum 13 bytes */
            if (frm_len >= 13)
            {
                evt.type = HMI_EVENT_RTC;
                evt.param_len = 7;
                memcpy(evt.param, &frame_buf[2], 7);
            }
            break;

        default:
            /* unknown command type — silently ignored */
            break;
        }

        /* ── Push valid event + invoke legacy callback ─────────── */
        if (evt.type != HMI_EVENT_NONE)
        {
            hmi_event_push(&evt);

            /* Dispatch to legacy NotifyXxx() callbacks */
            switch (evt.type)
            {
            case HMI_EVENT_SCREEN_CHANGE:
                NotifyScreen(evt.screen_id);
                break;
            case HMI_EVENT_CONTROL_NOTIFY:
                switch (evt.ctrl_type)
                {
                case 0x10:
                    NotifyButton(evt.screen_id, evt.control_id, evt.state);
                    break;
                case 0x11:
                    NotifyText(evt.screen_id, evt.control_id, evt.param);
                    break;
                case 0x12:
                    NotifyProgress(evt.screen_id, evt.control_id, evt.value);
                    break;
                case 0x13:
                    NotifySlider(evt.screen_id, evt.control_id, evt.value);
                    break;
                case 0x14:
                    NotifyMeter(evt.screen_id, evt.control_id, evt.value);
                    break;
                default:
                    break;
                }
                break;
            case HMI_EVENT_TOUCH:
                NotifyTouchXY(evt.state ? 1 : 3, evt.x, evt.y);
                break;
            case HMI_EVENT_RTC:
                if (evt.param_len >= 7)
                    NotifyReadRTC(evt.param[0], evt.param[1], evt.param[2],
                                  evt.param[3], evt.param[4], evt.param[5], evt.param[6]);
                break;
            default:
                break;
            }
        }

    reset_frame:
        /* ── Prepare for next frame ───────────────────────────────── */
        frame_pos = 0;
        tail_state = 0;
        noise_bytes = 0;
    }
}

/* ================================================================ */
/*  Weak default implementations for NotifyXxx() callbacks.           */
/*  Override any of these in the application layer to receive events  */
/*  without using the HmiEvent queue.                                 */
/* ================================================================ */

__weak void NotifyHandShake(void) {}

__weak void ProcessMessage(uint8_t cmd_type, uint8_t ctrl_msg,
                           uint16_t screen_id, uint16_t control_id,
                           uint8_t control_type, const uint8_t *param, uint16_t param_len)
{
    (void)cmd_type;
    (void)ctrl_msg;
    (void)screen_id;
    (void)control_id;
    (void)control_type;
    (void)param;
    (void)param_len;
}

__weak void NotifyScreen(uint16_t screen_id)
{
    (void)screen_id;
}

__weak void NotifyTouchXY(uint8_t press, uint16_t x, uint16_t y)
{
    (void)press;
    (void)x;
    (void)y;
}

__weak void NotifyButton(uint16_t screen_id, uint16_t control_id, uint8_t state)
{
    (void)screen_id;
    (void)control_id;
    (void)state;
}

__weak void NotifyText(uint16_t screen_id, uint16_t control_id, const uint8_t *str)
{
    (void)screen_id;
    (void)control_id;
    (void)str;
}

__weak void NotifyProgress(uint16_t screen_id, uint16_t control_id, uint32_t value)
{
    (void)screen_id;
    (void)control_id;
    (void)value;
}

__weak void NotifySlider(uint16_t screen_id, uint16_t control_id, uint32_t value)
{
    (void)screen_id;
    (void)control_id;
    (void)value;
}

__weak void NotifyMeter(uint16_t screen_id, uint16_t control_id, uint32_t value)
{
    (void)screen_id;
    (void)control_id;
    (void)value;
}

__weak void NotifyMenu(uint16_t screen_id, uint16_t control_id, uint8_t item, uint8_t state)
{
    (void)screen_id;
    (void)control_id;
    (void)item;
    (void)state;
}

__weak void NotifySelector(uint16_t screen_id, uint16_t control_id, uint8_t item)
{
    (void)screen_id;
    (void)control_id;
    (void)item;
}

__weak void NotifyTimer(uint16_t screen_id, uint16_t control_id)
{
    (void)screen_id;
    (void)control_id;
}

__weak void NotifyReadFlash(uint8_t status, uint8_t *data, uint16_t length)
{
    (void)status;
    (void)data;
    (void)length;
}

__weak void NotifyWriteFlash(uint8_t status)
{
    (void)status;
}

__weak void NotifyReadRTC(uint8_t year, uint8_t month, uint8_t week,
                          uint8_t day, uint8_t hour,
                          uint8_t minute, uint8_t second)
{
    (void)year;
    (void)month;
    (void)week;
    (void)day;
    (void)hour;
    (void)minute;
    (void)second;
}
