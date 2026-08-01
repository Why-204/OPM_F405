#ifndef _OPM_STORAGE_H
#define _OPM_STORAGE_H

#include <stdbool.h>
#include "opm_protocol.h"

#ifdef __cplusplus
extern "C"
{
#endif

bool opm_storage_load(OpmDeviceState *dev);
bool opm_storage_save(const OpmDeviceState *dev);

#ifdef __cplusplus
}
#endif

#endif /* _OPM_STORAGE_H */
