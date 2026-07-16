/**
 * @file    HMI_Task.c
 * @brief   HMI control task — multi-channel optical power meter DEMO.
 *
 *          Screens: 0=startup, 1=2ch, 2=4ch, 3=8ch
 *          Per‑channel IDs:  n0-n6  (n=1..8 : Chan/WL/WLunit/Pwr/Pwrunit/BtnSel/BtnWL)
 *          Global buttons:   1-5    (Channel/Wavelength/Unit/Cal/Local)
 */

#include "HMI_Task.h"
#include "ADC_Task.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ================================================================ */
/*  Compile‑time configuration                                      */
/* ================================================================ */
const uint8_t hmi_num_ch = (uint8_t)HMI_CFG_CHANNELS; /* 2 / 4 / 8 */

/* ── screen matching channel count ──────────────────────────────── */
static uint8_t hmi_screen_for_count(void)
{
    switch (hmi_num_ch)
    {
    case 2:
        return 1U;
    case 4:
        return 2U;
    case 8:
        return 3U;
    default:
        return 0U;
    }
}

/* ================================================================ */
/*  Global buttons (same IDs on every screen)                        */
/* ================================================================ */
#define BTN_CHANNEL 1U
#define BTN_WAVELENGTH 2U
#define BTN_UNIT 3U
#define BTN_CALIBRATION 4U
#define BTN_LOCAL 5U

/* ================================================================ */
/*  Constants                                                        */
/* ================================================================ */
#define BLINK_PERIOD 50U
#define POWER_BLINK_PERIOD 25U
#define REFRESH_TICKS 200U   /* ms between display refreshes     */
#define CHANNEL_RATE_MS 400U /* min interval for BTN_CHANNEL     */
#define CAL_THRESH_CDBM (-10000)
#define KB_X 200U
#define KB_Y 140U
#define KB_MAXLEN 6U

#define COLOR_DEFAULT 0x0861U

/* per‑channel highlight colours (index 0 → ch1, …, 7 → ch8) */
static const uint16_t ch_colors[8] = {
    0xFCC0U,
    0x9B3FU,
    0x3666U,
    0x64DFU,
    0xFB39U,
    0x79FFU,
    0xCE60U,
    0x0673U,
};

/* ================================================================ */
/*  Global data (extern in HMI_Task.h)                               */
/* ================================================================ */
HmiChannel hmi_ch[8];
uint8_t hmi_cur_ch;
uint8_t hmi_mode;
uint8_t hmi_cur_screen;

/* ================================================================ */
/*  Application state                                                */
/* ================================================================ */
typedef enum
{
    ST_STARTUP,
    ST_IDLE,
    ST_KEYBOARD_WAIT,
} AppState;

static AppState g_state = ST_STARTUP;
static uint8_t g_kb_ch;
static bool g_scr_ok; /* screen-change confirmed               */

/* ================================================================ */
/*  Display‑update throttle                                          */
/* ================================================================ */
static uint32_t g_next_refresh;

/* ================================================================ */
/*  Helpers — channel‑ID arithmetic                                  */
/* ================================================================ */

/** Base control ID for channel N (1‑based).  e.g. ch=1 → 10, ch=3 → 30 */
static inline uint8_t ch_base(uint8_t ch_idx)
{
    return (uint8_t)((ch_idx + 1U) * 10U);
}
static inline uint8_t ch_txt_wave(uint8_t ch_idx)
{
    return ch_base(ch_idx) + 1U;
}
static inline uint8_t ch_txt_pwr(uint8_t ch_idx)
{
    return ch_base(ch_idx) + 3U;
}
static inline uint8_t ch_txt_punit(uint8_t ch_idx)
{
    return ch_base(ch_idx) + 4U;
}
static inline uint8_t ch_txt_current(uint8_t ch_idx)
{
    return ch_base(ch_idx) + 6U;
}

#define STARTUP_RETRY_MS 1000U
#define STARTUP_TIMEOUT 3000U

/* ================================================================ */
/*  Forward declarations                                             */
/* ================================================================ */
static void hmi_init_data(void);
static void hmi_refresh_all(void);
static void hmi_refresh_channel(uint8_t idx);
static void hmi_process_events(void);
static void hmi_handle_keyboard_data(const uint8_t *str, uint8_t len);
static void hmi_cancel_keyboard(void);
static void hmi_show_keyboard(uint8_t ch_idx);
static void hmi_toggle_unit(void);
static void hmi_do_calibration(void);
static void hmi_switch_channel(uint8_t new_ch, bool highlight);
static void hmi_update_channel_highlight(void);
static void hmi_format_power(char *buf, uint8_t idx);
static void hmi_format_dbma(char *buf, uint8_t idx);
static void hmi_format_ma(char *buf, uint8_t idx);

static osThreadId_t s_hmi_ctrl_thread;

static const osThreadAttr_t s_hmi_ctrl_attr = {
    .name = "hmi_ctrl",
    .stack_size = 2048U,
    .priority = (osPriority_t)osPriorityNormal,
};

/* ================================================================ */
/*  Task bootstrap                                                   */
/* ================================================================ */
void hmi_task_init(void)
{
    if (s_hmi_ctrl_thread == NULL)
    {
        s_hmi_ctrl_thread = osThreadNew(hmi_control_task, NULL, &s_hmi_ctrl_attr);
    }
}

/* ================================================================ */
/*  Main task                                                        */
/* ================================================================ */
void hmi_control_task(void *arg)
{
    (void)arg;
    osDelay(500); /* wait for HMI screen power-up before talking to it */
    hmi_uart_os_init();
    hmi_init_data();
    hmi_cur_screen = 0U;
    g_scr_ok = false;
    g_state = ST_STARTUP;

    /* ── Startup: block until screen confirms or timeout ──────── */
    {
        uint32_t t0 = osKernelSysTick();
        uint32_t last_send = t0 - STARTUP_RETRY_MS; /* trigger immediate send */
        uint8_t target = hmi_screen_for_count();

        while (g_state == ST_STARTUP)
        {
            /* always drain events — needed for screen-change + any early buttons */
            hmi_process_events();

            if (osKernelSysTick() - last_send >= STARTUP_RETRY_MS)
            {
                SetScreen(target);
                last_send = osKernelSysTick();
            }

            if (osKernelSysTick() - t0 >= STARTUP_TIMEOUT)
            {
                g_scr_ok = true;
            }

            if (g_scr_ok)
            {
                hmi_cur_screen = target;
                hmi_refresh_all();
                osDelay(100); /* drain TX before highlight */
                hmi_update_channel_highlight();
                g_next_refresh = osKernelSysTick() + REFRESH_TICKS;
                g_state = ST_IDLE;
                break;
            }

            osDelay(200);
        }
    }

    /* ── Main loop ────────────────────────────────────────────── */
    while (1)
    {
        hmi_process_events();

        if (osKernelSysTick() >= g_next_refresh)
        {
            for (uint8_t i = 0U; i < hmi_num_ch && i < ADC_CHANNEL_COUNT; i++)
            {
                hmi_ch[i].raw_power = -adc_ch[i].voltage_v * 1000u;
            }
            hmi_refresh_all();
            g_next_refresh = osKernelSysTick() + REFRESH_TICKS;
        }

        osDelay(10);
    }
}

/* ================================================================ */
/*  Initialise demo data                                             */
/* ================================================================ */
static void hmi_init_data(void)
{
    hmi_cur_ch = 0U;
    hmi_mode = 0U;

    for (uint8_t i = 0U; i < hmi_num_ch; i++)
    {
        hmi_ch[i].wavelength = 131000UL;
        hmi_ch[i].raw_power = -1000; /* −10.00 dBm     */
        hmi_ch[i].zero_cal = CAL_THRESH_CDBM;
        hmi_ch[i].unit = 0U;
        hmi_ch[i].power_blink = false;
    }
}

/* ================================================================ */
/*  Power computation helpers (subtraction in mW domain)             */
/* ================================================================ */

/** Convert centi‑dBm → mW (float). */
static float hmi_cdbm_to_mw(int32_t cdbm)
{
    return powf(10.0f, (float)cdbm / 1000.0f);
}

/** Compute actual power in mW:  raw_mW − cal_mW.
 *  When zero_cal ≤ CAL_THRESH_CDBM the calibration reference is
 *  effectively 0 mW → no subtraction. */
static float hmi_get_actual_mw(uint8_t idx)
{
    float mw_raw = hmi_cdbm_to_mw(hmi_ch[idx].raw_power);
    if (hmi_ch[idx].zero_cal <= CAL_THRESH_CDBM)
        return mw_raw;
    float mw_cal = hmi_cdbm_to_mw(hmi_ch[idx].zero_cal);
    return mw_raw - mw_cal;
}

/* ================================================================ */
/*  Power formatting                                                 */
/* ================================================================ */

/** Format a non‑negative mW value with auto‑scaling (µW / mW / W). */
static void hmi_format_mw_positive(char *buf, float mw)
{
    if (mw >= 1000.0f) /* W */
    {
        float w = mw / 1000.0f;
        uint32_t v = (uint32_t)(w * 1000.0f + 0.5f);
        snprintf(buf, 16, "%lu.%03lu W",
                 (unsigned long)(v / 1000U), (unsigned long)(v % 1000U));
    }
    else if (mw >= 1.0f) /* mW */
    {
        if (mw >= 100.0f)
            snprintf(buf, 16, "%lu mW", (unsigned long)(uint32_t)(mw + 0.5f));
        else if (mw >= 10.0f)
        {
            uint32_t v = (uint32_t)(mw * 10.0f + 0.5f);
            snprintf(buf, 16, "%lu.%01lu mW",
                     (unsigned long)(v / 10U), (unsigned long)(v % 10U));
        }
        else
        {
            uint32_t v = (uint32_t)(mw * 100.0f + 0.5f);
            snprintf(buf, 16, "%lu.%02lu mW",
                     (unsigned long)(v / 100U), (unsigned long)(v % 100U));
        }
    }
    else /* µW */
    {
        float uw = mw * 1000.0f;
        if (uw >= 100.0f)
            snprintf(buf, 16, "%lu µW", (unsigned long)(uint32_t)(uw + 0.5f));
        else if (uw >= 10.0f)
        {
            uint32_t v = (uint32_t)(uw * 10.0f + 0.5f);
            snprintf(buf, 16, "%lu.%01lu µW",
                     (unsigned long)(v / 10U), (unsigned long)(v % 10U));
        }
        else
        {
            uint32_t v = (uint32_t)(uw * 100.0f + 0.5f);
            snprintf(buf, 16, "%lu.%02lu µW",
                     (unsigned long)(v / 100U), (unsigned long)(v % 100U));
        }
    }
}

/** Format actual power according to current unit.
 *  All computation is in the mW domain; dBm is derived from mW. */
static void hmi_format_power(char *buf, uint8_t idx)
{
    float mw = hmi_get_actual_mw(idx);

    switch (hmi_ch[idx].unit)
    {
    case 0: /* dBm — derived from mW via 10·log₁₀              */
        if (mw <= 0.0f)
        {
            if (mw < 0.0f)
            {
                snprintf(buf, 16, "----");
                hmi_ch[idx].power_blink = true;
            }
            else /* mw == 0.0 → -∞ dBm */
            {
                snprintf(buf, 16, "-infinite");
                hmi_ch[idx].power_blink = false;
            }
        }
        else
        {
            float dbm = 10.0f * log10f(mw);
            int32_t centi = (int32_t)(dbm * 100.0f);
            /* split into integer / fractional parts safely */
            bool neg = (centi < 0);
            uint32_t abs = neg ? (uint32_t)(-centi) : (uint32_t)centi;
            uint32_t ip = abs / 100U;
            uint32_t fp = abs % 100U;
            snprintf(buf, 16, "%c%lu.%02lu",
                     neg ? '-' : ' ',
                     (unsigned long)ip, (unsigned long)fp);
            hmi_ch[idx].power_blink = false;
        }
        break;

    case 1: /* Auto — mW‑domain, allows negative                */
        if (mw < 0.0f)
        {
            /* negative: prefix '-' and format absolute value */
            float amw = -mw;
            char tmp[16];
            hmi_format_mw_positive(tmp, amw);
            snprintf(buf, 16, "-%s", tmp);
            hmi_ch[idx].power_blink = false;
        }
        else if (mw == 0.0f)
        {
            snprintf(buf, 16, "0");
            hmi_ch[idx].power_blink = false;
        }
        else
        {
            hmi_format_mw_positive(buf, mw);
            hmi_ch[idx].power_blink = false;
        }
        break;

    default:
        buf[0] = '?';
        buf[1] = '\0';
        break;
    }
}

/** Format channel current in dBmA:  I_dBmA = 100 * (V_channel - 2.3). */
static void hmi_format_dbma(char *buf, uint8_t idx)
{
    float idbma = 100.0f * (adc_ch[idx].voltage_v - 2.3f);
    int32_t centi = (int32_t)(idbma * 100.0f + (idbma >= 0.0f ? 0.5f : -0.5f));
    bool neg = (centi < 0);
    uint32_t a = neg ? (uint32_t)(-centi) : (uint32_t)centi;
    snprintf(buf, 16, "%c%lu.%02lu",
             neg ? '-' : ' ',
             (unsigned long)(a / 100U), (unsigned long)(a % 100U));
}

/** Format channel current magnitude in mA (numeric only).
 *  dBmA = 100 * (V_channel - 2.3);  I_mA = 10^(dBmA / 10). */
static void hmi_format_ma(char *buf, uint8_t idx)
{
    float dbma = 100.0f * (adc_ch[idx].voltage_v - 2.3f);
    float ma = powf(10.0f, dbma / 10.0f);
    if (ma < 0.0f)
        ma = -ma; /* absolute value (defensive) */

    if (ma >= 100.0f)
    {
        snprintf(buf, 16, "%lu", (unsigned long)(uint32_t)(ma + 0.5f));
    }
    else if (ma >= 10.0f)
    {
        uint32_t v = (uint32_t)(ma * 100.0f + 0.5f);
        snprintf(buf, 16, "%lu.%02lu",
                 (unsigned long)(v / 100U), (unsigned long)(v % 100U));
    }
    else if (ma >= 1.0f)
    {
        uint32_t v = (uint32_t)(ma * 1000.0f + 0.5f);
        snprintf(buf, 16, "%lu.%03lu",
                 (unsigned long)(v / 1000U), (unsigned long)(v % 1000U));
    }
    else
    {
        uint32_t v = (uint32_t)(ma * 10000.0f + 0.5f);
        snprintf(buf, 16, "%lu.%04lu",
                 (unsigned long)(v / 10000U), (unsigned long)(v % 10000U));
    }
}

/* ================================================================ */
/*  Display refresh                                                  */
/* ================================================================ */

static void hmi_refresh_all(void)
{
    for (uint8_t i = 0U; i < hmi_num_ch; i++)
    {
        hmi_refresh_channel(i);
        osDelay(30); /* let TX drain between channels (8ch safe) */
    }
}

static void hmi_refresh_channel(uint8_t idx)
{
    char buf[32];
    uint8_t scr = hmi_cur_screen;
    uint8_t base = ch_base(idx);
    uint8_t wave_id = ch_txt_wave(idx);
    uint8_t pwr_id = ch_txt_pwr(idx);
    uint8_t punit_id = ch_txt_punit(idx);

    (void)scr;

    /* Wavelength */
    {
        uint32_t ip = hmi_ch[idx].wavelength / 100UL;
        uint32_t fp = hmi_ch[idx].wavelength % 100UL;
        snprintf(buf, sizeof(buf), "%lu.%02lu", (unsigned long)ip, (unsigned long)fp);
        SetTextValue(scr, wave_id, (uchar *)buf);
    }

    /* Current absolute value (mA, numeric only):  dBmA = 100*(V-2.3), I_mA = 10^(dBmA/10) */
    hmi_format_ma(buf, idx);
    SetTextValue(scr, pwr_id, (uchar *)buf);

    /* Current value (dBmA):  I = 100 * (V_channel - 2.3) */
    hmi_format_dbma(buf, idx);
    SetTextValue(scr, ch_txt_current(idx), (uchar *)buf);

    /* Power blink */
    {
        bool blink = (hmi_ch[idx].unit == 0U) && hmi_ch[idx].power_blink;
        SetTextBlink(scr, pwr_id, blink ? POWER_BLINK_PERIOD : 0U);
    }

    /* Unit labels: "mA" for the current value box (14,24,...,84),
     *              "dBmA" for the log-value box (12,22,...,82) */
    SetTextValue(scr, punit_id, (uchar *)"mA");
    SetTextValue(scr, (uint8_t)(base + 2U), (uchar *)"dBmA");
}

/* ================================================================ */
/*  Event processing (state machine)                                 */
/* ================================================================ */
static void hmi_process_events(void)
{
    HmiEvent evt;

    while (hmi_event_pop(&evt))
    {
        /* ── Keyboard data ────────────────────────────────────── */
        if (evt.ctrl_type == 0x86)
        {
            if (g_state == ST_KEYBOARD_WAIT && evt.param_len > 0)
                hmi_handle_keyboard_data(evt.param, evt.param_len);
            continue;
        }

        /* ── Screen change ────────────────────────────────────── */
        if (evt.type == HMI_EVENT_SCREEN_CHANGE)
        {
            uint8_t target = hmi_screen_for_count();
            if (evt.screen_id == target && g_state == ST_STARTUP)
                g_scr_ok = true;
            else if (evt.screen_id == hmi_cur_screen)
                hmi_refresh_all();
            continue;
        }

        /* ── Button only ──────────────────────────────────────── */
        if (evt.type != HMI_EVENT_CONTROL_NOTIFY)
            continue;
        if (evt.ctrl_type != HMI_CTRL_BUTTON)
            continue;
        if (evt.state != 1U)
            continue;

        /* Safety: clamp cur_ch in case of corruption              */
        if (hmi_cur_ch >= hmi_num_ch)
            hmi_cur_ch = 0U;

        uint16_t cid = evt.control_id;

        /* Keyboard cancel */
        if (g_state == ST_KEYBOARD_WAIT)
            hmi_cancel_keyboard();

        /* ── Global buttons ───────────────────────────────────── */
        switch (cid)
        {
        case BTN_CHANNEL:
        {
            static uint32_t last_ch_press = 0;
            if (osKernelSysTick() - last_ch_press >= CHANNEL_RATE_MS)
            {
                last_ch_press = osKernelSysTick();
                hmi_switch_channel(
                    (hmi_cur_ch + 1U >= hmi_num_ch) ? 0U : (hmi_cur_ch + 1U),
                    true);
            }
            break;
        }
        case BTN_WAVELENGTH:
            hmi_show_keyboard(hmi_cur_ch);
            break;
        case BTN_UNIT:
            hmi_toggle_unit();
            break;
        case BTN_CALIBRATION:
            hmi_do_calibration();
            break;
        case BTN_LOCAL:
            hmi_mode = (hmi_mode == 0U) ? 1U : 0U;
            break;
        default:
        {
            /* ── Per‑channel buttons:  n5 / n6  (n = 1..8) ──── */
            uint8_t tens = (uint8_t)(cid / 10U); /* 1‑8    */
            uint8_t ones = (uint8_t)(cid % 10U); /* 5 or 6 */
            if (tens >= 1U && tens <= 8U && (ones == 5U || ones == 6U))
            {
                uint8_t ch = tens - 1U; /* 0‑7    */
                if (ch < hmi_num_ch)
                {
                    if (ones == 5U) /* select */
                        hmi_switch_channel(ch, false);
                    else /* wave   */
                    {
                        hmi_switch_channel(ch, false);
                        hmi_show_keyboard(ch);
                    }
                }
            }
            break;
        }
        }
    }
}

/* ================================================================ */
/*  Keyboard handling                                                */
/* ================================================================ */

/** Pop up the system keyboard and start blinking the target text. */
static void hmi_show_keyboard(uint8_t ch_idx)
{
    uint8_t scr = hmi_cur_screen;
    uint16_t wave_id = ch_txt_wave(ch_idx);

    SetTextBlink(scr, wave_id, BLINK_PERIOD);
    ShowKeyboard(1, KB_X, KB_Y, 0, 0, KB_MAXLEN);
    g_kb_ch = ch_idx;
    g_state = ST_KEYBOARD_WAIT;
}

static void hmi_handle_keyboard_data(const uint8_t *str, uint8_t len)
{
    uint8_t scr = hmi_cur_screen;
    uint16_t wave_id = ch_txt_wave(g_kb_ch);

    SetTextBlink(scr, wave_id, 0U);
    HideKeyboard();

    char tmp[10];
    uint8_t n = (len < 9U) ? len : 8U;
    memcpy(tmp, str, n);
    tmp[n] = '\0';

    char *dot = strchr(tmp, '.');
    uint32_t ip = 0UL, fp = 0UL;
    if (dot)
    {
        *dot = '\0';
        ip = (uint32_t)atoi(tmp);
        fp = (uint32_t)atoi(dot + 1);
        if (fp > 99UL)
            fp %= 100UL;
    }
    else
    {
        ip = (uint32_t)atoi(tmp);
    }
    uint32_t val = ip * 100UL + fp;
    if (val > 0UL && val <= 999999UL)
        hmi_ch[g_kb_ch].wavelength = val;

    hmi_refresh_channel(g_kb_ch);
    g_state = ST_IDLE;
}

static void hmi_cancel_keyboard(void)
{
    uint8_t scr = hmi_cur_screen;
    uint16_t wave_id = ch_txt_wave(g_kb_ch);
    SetTextBlink(scr, wave_id, 0U);
    HideKeyboard();
    g_state = ST_IDLE;
}

/* ================================================================ */
/*  Button action helpers                                            */
/* ================================================================ */

/** Toggle power unit for current channel: dBm ↔ Auto. */
static void hmi_toggle_unit(void)
{
    hmi_ch[hmi_cur_ch].unit = (hmi_ch[hmi_cur_ch].unit == 0U) ? 1U : 0U;
    hmi_refresh_channel(hmi_cur_ch);
}

/** Store current raw power as zero calibration. */
static void hmi_do_calibration(void)
{
    hmi_ch[hmi_cur_ch].zero_cal = hmi_ch[hmi_cur_ch].raw_power;
    hmi_refresh_channel(hmi_cur_ch);
}

static void hmi_switch_channel(uint8_t new_ch, bool highlight)
{
    if (new_ch != hmi_cur_ch && new_ch < hmi_num_ch)
    {
        uint8_t old = hmi_cur_ch;
        hmi_cur_ch = new_ch;
        if (highlight)
            hmi_update_channel_highlight();
        /* only refresh the two affected channels, not all */
        hmi_refresh_channel(old);
        hmi_refresh_channel(new_ch);
    }
}

static void hmi_update_channel_highlight(void)
{
    uint8_t scr = hmi_cur_screen;
    for (uint8_t i = 0U; i < hmi_num_ch; i++)
    {
        uint8_t cid = ch_base(i);
        uint16_t color = (i == hmi_cur_ch) ? ch_colors[i] : COLOR_DEFAULT;
        SetControlBackColor(scr, cid, color);
    }
}
