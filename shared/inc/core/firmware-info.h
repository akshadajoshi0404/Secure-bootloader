#ifndef INC_FIRMWARE_INFO_H
#define INC_FIRMWARE_INFO_H

#include "common-defines.h"
#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/vector.h>

#define BOOTLOADER_SIZE             (0x8000U)
#define MAIN_APP_START_ADDRESS      (FLASH_BASE + BOOTLOADER_SIZE)
#define MAX_FW_LENGTH               ((1024*256) - BOOTLOADER_SIZE)
#define DEVICE_ID                   (0x42)

#define FWINFO_ADDRESS              (MAIN_APP_START_ADDRESS + sizeof(vector_table_t))
#define FWINFO_VALIDATE_FROM        (FWINFO_ADDRESS + sizeof(firmware_info_t))
#define VALIDATE_LENGTH(fw_length)  (fw_length - sizeof(vector_table_t) - sizeof(firmware_info_t))
#define FIRMWARE_INFO_SENTINEL      (0xDEADC0DE)

typedef struct firmware_info_t
{
    uint32_t sentinel;
    uint32_t device_id;
    uint32_t firmware_version;
    uint32_t length;
    uint32_t reserved[4];
    uint32_t CRC32;
}firmware_info_t; //first page of application firmware must be reserved for this struct, and the vector table must be located before this struct, so that the bootloader can read the firmware info and validate the firmware before jumping to the main application.



#endif // INC_FIRMWARE_INFO_H