/**
 * @file    cmd_process.h
 * @brief   HMI notification callbacks for control / touch / RTC events.
 *          These weak functions are invoked by hmi_driver_unpack() after
 *          a complete frame has been parsed.  The application layer can
 *          override any of them to receive HMI events directly.
 *
 *          Alternatively the application can use the HmiEvent queue
 *          (hmi_event_pop / hmi_event_available) for a decoupled design.
 *
 * @version 2.0  (refactored for STM32F4 / HAL)
 * @date    2026-05-27
 * @copyright  Dacai Technology Co., Ltd.
 */

#ifndef _CMD_PROCESS_H
#define _CMD_PROCESS_H

#include <stdint.h>

/* ================================================================ */
/*  Notification type constants (match VisualTFT protocol)          */
/* ================================================================ */
#define NOTIFY_TOUCH_PRESS 0x01       /* touch press notification       */
#define NOTIFY_TOUCH_RELEASE 0x03     /* touch release notification     */
#define NOTIFY_WRITE_FLASH_OK 0x0C    /* flash write success            */
#define NOTIFY_WRITE_FLASH_FAILD 0x0D /* flash write failed             */
#define NOTIFY_READ_FLASH_OK 0x0B     /* flash read success             */
#define NOTIFY_READ_FLASH_FAILD 0x0F  /* flash read failed              */
#define NOTIFY_MENU 0x14              /* menu event notification        */
#define NOTIFY_TIMER 0x43             /* timer timeout notification     */
#define NOTIFY_CONTROL 0xB1           /* control event notification     */
#define NOTIFY_READ_RTC 0xF7          /* RTC time read response         */
#define NOTIFY_HANDSHAKE 0x55         /* handshake notification         */

#define MSG_GET_CURRENT_SCREEN 0x01 /* screen-id change notification  */
#define MSG_GET_DATA 0x11           /* control data notification      */

/* helper: extract 16-bit / 32-bit big-endian from byte buffer */
#define PTR2U16(PTR) ((((uint8_t *)(PTR))[0] << 8) | ((uint8_t *)(PTR))[1])
#define PTR2U32(PTR) ((((uint8_t *)(PTR))[0] << 24) | (((uint8_t *)(PTR))[1] << 16) | \
                      (((uint8_t *)(PTR))[2] << 8) | ((uint8_t *)(PTR))[3])

/* ================================================================ */
/*  Control type enum (matches VisualTFT control IDs)                */
/* ================================================================ */
enum CtrlType
{
    kCtrlUnknown = 0x00,
    kCtrlButton = 0x10,    /* Button                 */
    kCtrlText = 0x11,      /* Text                   */
    kCtrlProgress = 0x12,  /* Progress bar           */
    kCtrlSlider = 0x13,    /* Slider                 */
    kCtrlMeter = 0x14,     /* Meter                  */
    kCtrlDropList = 0x15,  /* Drop-down list         */
    kCtrlAnimation = 0x16, /* Animation              */
    kCtrlRTC = 0x17,       /* RTC display            */
    kCtrlGraph = 0x18,     /* Graph control          */
    kCtrlTable = 0x19,     /* Table control          */
    kCtrlMenu = 0x1A,      /* Menu control           */
    kCtrlSelector = 0x1B,  /* Selector control       */
    kCtrlQRCode = 0x1C,    /* QR code                */
};

/* ================================================================ */
/*  HMI notification callbacks (weak — override in application)     */
/* ================================================================ */

/** Handshake response from screen */
void NotifyHandShake(void);

/** Raw message processor (legacy, prefer the per-type callbacks) */
void ProcessMessage(uint8_t cmd_type, uint8_t ctrl_msg,
                    uint16_t screen_id, uint16_t control_id,
                    uint8_t control_type, const uint8_t *param, uint16_t param_len);

/** Screen-changed notification */
void NotifyScreen(uint16_t screen_id);

/** Touch press / release notification */
void NotifyTouchXY(uint8_t press, uint16_t x, uint16_t y);

/** Button control notification (state: 0=release, 1=press) */
void NotifyButton(uint16_t screen_id, uint16_t control_id, uint8_t state);

/** Text control notification (NUL-terminated string) */
void NotifyText(uint16_t screen_id, uint16_t control_id, const uint8_t *str);

/** Progress-bar control notification (value: 0..range) */
void NotifyProgress(uint16_t screen_id, uint16_t control_id, uint32_t value);

/** Slider control notification (value: 0..range) */
void NotifySlider(uint16_t screen_id, uint16_t control_id, uint32_t value);

/** Meter control notification */
void NotifyMeter(uint16_t screen_id, uint16_t control_id, uint32_t value);

/** Menu control notification */
void NotifyMenu(uint16_t screen_id, uint16_t control_id, uint8_t item, uint8_t state);

/** Selector control notification */
void NotifySelector(uint16_t screen_id, uint16_t control_id, uint8_t item);

/** Timer timeout notification */
void NotifyTimer(uint16_t screen_id, uint16_t control_id);

/** Read-user-flash status callback */
void NotifyReadFlash(uint8_t status, uint8_t *data, uint16_t length);

/** Write-user-flash status callback */
void NotifyWriteFlash(uint8_t status);

/** Read-RTC response (all values BCD-encoded) */
void NotifyReadRTC(uint8_t year, uint8_t month, uint8_t week,
                   uint8_t day, uint8_t hour,
                   uint8_t minute, uint8_t second);

#endif /* _CMD_PROCESS_H */
