
#ifndef _HMI_TASK_H
#define _HMI_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "cmsis_os.h"
#include "hmi_user_uart.h"
#include "hmi_driver.h"

/* ── Compile‑time channel count ────────────────────────────────── */
typedef enum
{
    Channels2 = 2,
    Channels4 = 4,
    Channels8 = 8,
} HmiChannelCount;

#define HMI_CFG_CHANNELS Channels8 /* ← change here to reconfigure */

/* ── Public data structures ────────────────────────────────────── */

/** Per‑channel optical power meter data (extern, writable by any module). */
typedef struct
{
    uint32_t wavelength; /* centi‑nm (×100), e.g. 131000 = 1310.00 nm */
    int32_t raw_power;   /* centi‑dBm (×100), from sensor       */
    int32_t zero_cal;    /* centi‑dBm (×100), calibration ref   */
    uint8_t unit;        /* 0 = dBm,  1 = Auto (uW / mW / W)  */
    bool power_blink;    /* true → power text should blink      */
} HmiChannel;

extern HmiChannel hmi_ch[8];     /* up to 8 channels               */
extern const uint8_t hmi_num_ch; /* active channel count (2/4/8)   */
extern uint8_t hmi_cur_ch;       /* currently selected channel (0‑N)*/
extern uint8_t hmi_mode;         /* 0 = Local,  1 = Controlled     */
extern uint8_t hmi_cur_screen;   /* active screen ID               */

/* ── Public interface ──────────────────────────────────────────── */

/** Create the HMI control task.  Call once during init. */
void hmi_task_init(void);

/** HMI control task entry point (passed to osThreadCreate). */
void hmi_control_task(void const *arg);

#endif /* _HMI_TASK_H */
