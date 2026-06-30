/**
 * @file    hmi_driver.h
 * @brief   HMI (Dacai serial screen) protocol driver — command API + frame
 *          pack / unpack.  All TX functions write into an internal pack
 *          buffer and are sent via hmi_uart_send().
 *
 *          Protocol version: V5.1, FIRMWARE_VER 921.
 *
 * @version 2.0  (refactored for STM32F4 / HAL)
 * @date    2026-05-27
 */

#ifndef _HMI_DRIVER_
#define _HMI_DRIVER_

#include <stdint.h>
#include <stdbool.h>
#include "hmi_user_uart.h"
#include "cmd_queue.h"
#include "cmd_process.h"

/* ================================================================ */
/*  Protocol configuration                                           */
/* ================================================================ */
#define FIRMWARE_VER 921 /* must match the screen firmware version */
#define CRC16_ENABLE 0	 /* set 1 to enable CRC16 (VisualTFT option) */
#define CMD_MAX_SIZE 256 /* max single frame payload (including header+tail) */
#define SD_FILE_EN 0	 /* SD-card file commands disabled by default */

/* ================================================================ */
/*  Compatibility typedefs (preserved for legacy callers)            */
/* ================================================================ */
typedef uint8_t uchar;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int16_t int16;
typedef int32_t int32;

/** HMI command. */
void LockDeviceConfig(void);

/** HMI command. */
void UnlockDeviceConfig(void);

/** HMI command. */
void SetCommBps(uint8 option);

/** HMI command. */
void SetHandShake(void);

/** HMI command. */
void SetFcolor(uint16 color);

/** HMI command. */
void SetBcolor(uint16 color);

/** HMI command. */
void GUI_CleanScreen(void);

/** HMI command. */
void SetTextSpace(uint8 x_w, uint8 y_w);

/** HMI command. */
void SetFont_Region(uint8 enable, uint16 width, uint16 height);

/** HMI command. */
void SetFilterColor(uint16 fillcolor_dwon, uint16 fillcolor_up);

/** HMI command. */
void DisText(uint16 x, uint16 y, uint8 back, uint8 font, uchar *strings);

/** HMI command. */
void DisCursor(uint8 enable, uint16 x, uint16 y, uint8 width, uint8 height);

/** HMI command. */
void DisFull_Image(uint16 image_id, uint8 masken);

/** HMI command. */
void DisArea_Image(uint16 x, uint16 y, uint16 image_id, uint8 masken);

/** HMI command. */
void DisCut_Image(uint16 x, uint16 y, uint16 image_id, uint16 image_x, uint16 image_y,
				  uint16 image_l, uint16 image_w, uint8 masken);

/** HMI command. */
void DisFlashImage(uint16 x, uint16 y, uint16 flashimage_id, uint8 enable, uint8 playnum);

/** HMI command. */
void GUI_Dot(uint16 x, uint16 y);

/** HMI command. */
void GUI_Line(uint16 x0, uint16 y0, uint16 x1, uint16 y1);

/** HMI command. */
void GUI_ConDots(uint8 mode, uint16 *dot, uint16 dot_cnt);

/** HMI command. */
void GUI_Circle(uint16 x0, uint16 y0, uint16 r);

/** HMI command. */
void GUI_CircleFill(uint16 x0, uint16 y0, uint16 r);

/** HMI command. */
void GUI_Arc(uint16 x, uint16 y, uint16 r, uint16 sa, uint16 ea);

/** HMI command. */
void GUI_Rectangle(uint16 x0, uint16 y0, uint16 x1, uint16 y1);

/** HMI command. */
void GUI_RectangleFill(uint16 x0, uint16 y0, uint16 x1, uint16 y1);

/** HMI command. */
void GUI_Ellipse(uint16 x0, uint16 y0, uint16 x1, uint16 y1);

/** HMI command. */
void GUI_EllipseFill(uint16 x0, uint16 y0, uint16 x1, uint16 y1);

/** HMI command. */
void SetBackLight(uint8 light_level);

/** HMI command. */
void SetBuzzer(uint8 time);

/** HMI command. */
void SetTouchPaneOption(uint8 enbale, uint8 beep_on, uint8 work_mode, uint8 press_calibration);

/** HMI command. */
void CalibrateTouchPane(void);

/** HMI command. */
void TestTouchPane(void);

/** HMI command. */
void WriteLayer(uint8 layer);

/** HMI command. */
void DisplyLayer(uint8 layer);

/** HMI command. */
void ClearLayer(uint8 layer);

/** HMI command. */
void WriteUserFlash(uint32 startAddress, uint16 length, uint8 *_data);

/** HMI command. */
void ReadUserFlash(uint32 startAddress, uint16 length);

/** HMI command. */
void CopyLayer(uint8 src_layer, uint8 dest_layer);

/** HMI command. */
void SetScreen(uint16 screen_id);

/** HMI command. */
void GetScreen(uint16 screen_id);

/** HMI command. */
void SetScreenUpdateEnable(uint8 enable);

/** HMI command. */
void SetControlFocus(uint16 screen_id, uint16 control_id, uint8 focus);

/** HMI command. */
void SetControlVisiable(uint16 screen_id, uint16 control_id, uint8 visible);

/** HMI command. */
void SetControlEnable(uint16 screen_id, uint16 control_id, uint8 enable);

/** HMI command. */
void GetControlValue(uint16 screen_id, uint16 control_id);

/** HMI command. */
void SetButtonValue(uint16 screen_id, uint16 control_id, uchar value);

/** HMI command. */
void SetTextValue(uint16 screen_id, uint16 control_id, uchar *str);

#if FIRMWARE_VER >= 908

/** HMI command. */
void SetTextInt32(uint16 screen_id, uint16 control_id, uint32 value, uint8 sign, uint8 fill_zero);

/** HMI command. */
void SetTextFloat(uint16 screen_id, uint16 control_id, float value, uint8 precision, uint8 show_zeros);

#endif

/** HMI command. */
void SetProgressValue(uint16 screen_id, uint16 control_id, uint32 value);

/** HMI command. */
void SetMeterValue(uint16 screen_id, uint16 control_id, uint32 value);

/** HMI command. */
void Set_picMeterValue(uint16 screen_id, uint16 control_id, uint16 value);

/** HMI command. */

void SetSliderValue(uint16 screen_id, uint16 control_id, uint32 value);

/** HMI command. */
void SetSelectorValue(uint16 screen_id, uint16 control_id, uint8 item);

/** HMI command. */
void AnimationStart(uint16 screen_id, uint16 control_id);

/** HMI command. */
void AnimationStop(uint16 screen_id, uint16 control_id);

/** HMI command. */
void AnimationPause(uint16 screen_id, uint16 control_id);

/** HMI command. */
void AnimationPlayFrame(uint16 screen_id, uint16 control_id, uint8 frame_id);

/** HMI command. */
void AnimationPlayPrev(uint16 screen_id, uint16 control_id);

/** HMI command. */
void AnimationPlayNext(uint16 screen_id, uint16 control_id);

/** HMI command. */
void GraphChannelAdd(uint16 screen_id, uint16 control_id, uint8 channel, uint16 color);

/** HMI command. */
void GraphChannelDel(uint16 screen_id, uint16 control_id, uint8 channel);

/** HMI command. */
void GraphChannelDataAdd(uint16 screen_id, uint16 control_id, uint8 channel, uint8 *pData, uint16 nDataLen);

/** HMI command. */
void GraphChannelDataClear(uint16 screen_id, uint16 control_id, uint8 channel);

/** HMI command. */
void GraphSetViewport(uint16 screen_id, uint16 control_id, int16 x_offset, uint16 x_mul, int16 y_offset, uint16 y_mul);

/** HMI command. */
void BatchBegin(uint16 screen_id);

/** HMI command. */
void BatchSetButtonValue(uint16 control_id, uint8 state);

/** HMI command. */
void BatchSetProgressValue(uint16 control_id, uint32 value);

/** HMI command. */
void BatchSetSliderValue(uint16 control_id, uint32 value);

/** HMI command. */
void BatchSetMeterValue(uint16 control_id, uint32 value);

/** HMI command. */
void BatchSetText(uint16 control_id, uchar *strings);

/** HMI command. */
void BatchSetFrame(uint16 control_id, uint16 frame_id);

#if FIRMWARE_VER >= 921

/** HMI command. */
void BatchSetVisible(uint16 control_id, uint8 visible);

/** HMI command. */
void BatchSetEnable(uint16 control_id, uint8 enable);

#endif

/** HMI command. */
void BatchEnd(void);

/** HMI command. */
void SeTimer(uint16 screen_id, uint16 control_id, uint32 timeout);

/** HMI command. */
void StartTimer(uint16 screen_id, uint16 control_id);

/** HMI command. */
void StopTimer(uint16 screen_id, uint16 control_id);

/** HMI command. */
void PauseTimer(uint16 screen_id, uint16 control_id);

/** HMI command. */
void SetControlBackColor(uint16 screen_id, uint16 control_id, uint16 color);

/** HMI command. */
void SetControlForeColor(uint16 screen_id, uint16 control_id, uint16 color);

/** HMI command. */
void ShowPopupMenu(uint16 screen_id, uint16 control_id, uint8 show, uint16 focus_control_id);

/** HMI command. */
void ShowKeyboard(uint8 show, uint16 x, uint16 y, uint8 type, uint8 option, uint8 max_len);

/** HMI command — convenience: hide system keyboard. */
void HideKeyboard(void);

/** HMI command — set text control blink cycle (10ms unit, 0 = stop). */
void SetTextBlink(uint16 screen_id, uint16 control_id, uint16 cycle);

#if FIRMWARE_VER >= 914
/** HMI command. */
void SetLanguage(uint8 ui_lang, uint8 sys_lang);
#endif

#if FIRMWARE_VER >= 917
/** HMI command. */
void FlashBeginSaveControl(uint32 version, uint32 address);

/** HMI command. */
void FlashSaveControl(uint16 screen_id, uint16 control_id);

/** HMI command. */
void FlashEndSaveControl(void);

/** HMI command. */
void FlashRestoreControl(uint32 version, uint32 address);
#endif

#if FIRMWARE_VER >= 921
/** HMI command. */
void HistoryGraph_SetValueInt8(uint16 screen_id, uint16 control_id, uint8 *value, uint8 channel);

/** HMI command. */
void HistoryGraph_SetValueInt16(uint16 screen_id, uint16 control_id, uint16 *value, uint8 channel);

/** HMI command. */
void HistoryGraph_SetValueInt32(uint16 screen_id, uint16 control_id, uint32 *value, uint8 channel);

/** HMI command. */
void HistoryGraph_SetValueFloat(uint16 screen_id, uint16 control_id, float *value, uint8 channel);

/** HMI command. */
void HistoryGraph_EnableSampling(uint16 screen_id, uint16 control_id, uint8 enable);

/** HMI command. */
void HistoryGraph_ShowChannel(uint16 screen_id, uint16 control_id, uint8 channel, uint8 show);

/** HMI command. */
void HistoryGraph_SetTimeLength(uint16 screen_id, uint16 control_id, uint16 sample_count);

/** HMI command. */
void HistoryGraph_SetTimeFullScreen(uint16 screen_id, uint16 control_id);

/** HMI command. */
void HistoryGraph_SetTimeZoom(uint16 screen_id, uint16 control_id, uint16 zoom, uint16 max_zoom, uint16 min_zoom);
#endif

#if SD_FILE_EN
/** HMI command. */
void SD_IsInsert(void);

#define FA_READ 0x01		  // �ɶ�ȡ
#define FA_WRITE 0x02		  // ��д��
#define FA_CREATE_NEW 0x04	  // �������ļ�������ļ��Ѿ����ڣ��򷵻�ʧ��
#define FA_CREATE_ALWAYS 0x08 // �������ļ�������ļ��Ѿ����ڣ��򸲸�
#define FA_OPEN_EXISTING 0x00 // ���ļ�������ļ������ڣ��򷵻�ʧ��
#define FA_OPEN_ALWAYS 0x10	  // ���ļ�������ļ������ڣ��򴴽����ļ�

/** HMI command. */
void SD_CreateFile(uint8 *filename, uint8 mode);

/** HMI command. */
void SD_CreateFileByTime(uint8 *ext);

/** HMI command. */
void SD_WriteFile(uint8 *buffer, uint16 dlc);

/** HMI command. */
void SD_ReadFile(uint32 offset, uint16 dlc);

/** HMI command. */
void SD_GetFileSize();

/** HMI command. */
void SD_CloseFile();
#endif

/** HMI command. */
void Record_SetEvent(uint16 screen_id, uint16 control_id, uint16 value, uint8 *time);

/** HMI command. */
void Record_ResetEvent(uint16 screen_id, uint16 control_id, uint16 value, uint8 *time);

/** HMI command. */
void Record_Add(uint16 screen_id, uint16 control_id, uint8 *record);

/** HMI command. */
void Record_Clear(uint16 screen_id, uint16 control_id);

/** HMI command. */
void Record_SetOffset(uint16 screen_id, uint16 control_id, uint16 offset);

/** HMI command. */
void Record_GetCount(uint16 screen_id, uint16 control_id);

/** HMI command. */
void ReadRTC(void);

/** HMI command. */
void PlayMusic(uint8 *buffer);

/* ================================================================ */
/*  Frame pack / unpack API (new in v2.0)                            */
/* ================================================================ */

/**
 * Pack raw command payload into HMI frame (0xEE + payload + 0xFFFCFFFF)
 * and send via hmi_uart_send().  Used internally by all command functions.
 * @param data  Raw payload bytes (without header/tail)
 * @param len   Payload length in bytes
 */
void hmi_driver_pack_cmd(const uint8_t *data, uint16_t len);

/**
 * Unpack frames from the RX ring buffer.  For each complete frame,
 * parse it into an HmiEvent and push into the global event queue.
 * Additionally invokes the matching NotifyXxx() callback.
 *
 * @param rxbuf  Pointer to the RX ring buffer (bytes from UART ISR)
 */
void hmi_driver_unpack(HmiRxRingBuf *rxbuf);

/**
 * Reset the internal frame-parser state (frame_pos, tail_state).
 * Call after a UART error or prolonged silence to avoid parser lock-up
 * on a partial / corrupted frame.
 */
void hmi_driver_reset_frame(void);

#endif /* _HMI_DRIVER_ */
