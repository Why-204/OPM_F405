#include "opm_storage.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define OPM_STORAGE_FLASH_ADDR 0x080E0000UL
#define OPM_STORAGE_FLASH_SECTOR FLASH_SECTOR_11

#define OPM_STORAGE_MAGIC 0x4F504D4FU /* "OPMO" */
#define OPM_STORAGE_VERSION 1U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t crc32;
    uint8_t channel_count;
    uint8_t cal_wl_count;
    uint8_t reserved[2];
    uint16_t cal_wl[OPM_MAX_CAL_WL];
    float offset_db[OPM_MAX_CHANNELS][OPM_MAX_CAL_WL];
} OpmStorageImage;

static uint32_t crc32_calc(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0U; i < len; i++)
    {
        crc ^= p[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1UL) != 0UL)
            {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

static void storage_build_image(const OpmDeviceState *dev, OpmStorageImage *img)
{
    memset(img, 0, sizeof(*img));
    img->magic = OPM_STORAGE_MAGIC;
    img->version = OPM_STORAGE_VERSION;
    img->size = (uint16_t)sizeof(*img);
    img->channel_count = dev->channel_count;
    img->cal_wl_count = dev->cal_wl_count;
    memcpy(img->cal_wl, dev->cal_wl, sizeof(img->cal_wl));
    memcpy(img->offset_db, dev->offset_db, sizeof(img->offset_db));
    img->crc32 = 0U;
    img->crc32 = crc32_calc(img, (uint32_t)sizeof(*img));
}

static bool storage_image_valid(const OpmStorageImage *img)
{
    OpmStorageImage tmp;
    uint32_t saved_crc;

    if (img->magic != OPM_STORAGE_MAGIC ||
        img->version != OPM_STORAGE_VERSION ||
        img->size != (uint16_t)sizeof(*img) ||
        img->cal_wl_count > OPM_MAX_CAL_WL ||
        img->channel_count > OPM_MAX_CHANNELS)
    {
        return false;
    }

    tmp = *img;
    saved_crc = tmp.crc32;
    tmp.crc32 = 0U;
    return saved_crc == crc32_calc(&tmp, (uint32_t)sizeof(tmp));
}

static bool storage_flash_matches(const OpmStorageImage *img)
{
    const OpmStorageImage *flash_img = (const OpmStorageImage *)OPM_STORAGE_FLASH_ADDR;

    if (!storage_image_valid(flash_img))
    {
        return false;
    }

    return memcmp(flash_img, img, sizeof(*img)) == 0;
}

bool opm_storage_load(OpmDeviceState *dev)
{
    const OpmStorageImage *flash_img = (const OpmStorageImage *)OPM_STORAGE_FLASH_ADDR;

    if (dev == NULL || !storage_image_valid(flash_img))
    {
        return false;
    }
    if (flash_img->cal_wl_count != dev->cal_wl_count)
    {
        return false;
    }
    if (memcmp(flash_img->cal_wl, dev->cal_wl,
               (uint32_t)dev->cal_wl_count * sizeof(dev->cal_wl[0])) != 0)
    {
        return false;
    }

    memcpy(dev->offset_db, flash_img->offset_db, sizeof(dev->offset_db));
    return true;
}

bool opm_storage_save(const OpmDeviceState *dev)
{
    OpmStorageImage img;
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0U;
    HAL_StatusTypeDef st;

    if (dev == NULL)
    {
        return false;
    }
    if ((sizeof(img) % sizeof(uint32_t)) != 0U)
    {
        return false;
    }

    storage_build_image(dev, &img);
    if (storage_flash_matches(&img))
    {
        return true;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = OPM_STORAGE_FLASH_SECTOR;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    st = HAL_FLASHEx_Erase(&erase, &sector_error);

    if (st == HAL_OK && sector_error == 0xFFFFFFFFUL)
    {
        const uint32_t *src = (const uint32_t *)&img;
        uint32_t addr = OPM_STORAGE_FLASH_ADDR;
        for (uint32_t i = 0U; i < (uint32_t)(sizeof(img) / sizeof(uint32_t)); i++)
        {
            st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]);
            if (st != HAL_OK)
            {
                break;
            }
            addr += sizeof(uint32_t);
        }
    }

#if defined(__HAL_FLASH_DATA_CACHE_DISABLE) && defined(__HAL_FLASH_DATA_CACHE_RESET) && defined(__HAL_FLASH_DATA_CACHE_ENABLE)
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
#endif

    (void)HAL_FLASH_Lock();

    return st == HAL_OK && storage_flash_matches(&img);
}
