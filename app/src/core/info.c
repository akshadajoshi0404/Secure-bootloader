#include "core/firmware-info.h"

__attribute__((section(".firmware_info")))
firmware_info_t firmware_info = {
    .sentinel           = FIRMWARE_INFO_SENTINEL,
    .device_id          = DEVICE_ID,
    .firmware_version   = 0xFFFFFFFF, //version 1.0.0
    .length             = 0xFFFFFFFF, //to be filled by the build system or at runtime
    .reserved           = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}, //reserved for future use
    .CRC32              = 0xFFFFFFFF //to be calculated and filled by the build system or at runtime
};

