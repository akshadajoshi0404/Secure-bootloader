#ifndef INC_FIRMWARE_INFO_H
#define INC_FIRMWARE_INFO_H

#include "common-defines.h"
#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/vector.h>


#define ALIGNED(adress, alignment) (((address) + ((alignment) - 1)) & ~((alignment) - 1))
#define BOOTLOADER_SIZE             (0x8000U)
#define MAIN_APP_START_ADDRESS      (FLASH_BASE + BOOTLOADER_SIZE)
#define MAX_FW_LENGTH               ((1024*256) - BOOTLOADER_SIZE)
#define DEVICE_ID                   (0x42)

#define FIRMWARE_INFO_SENTINEL      (0xDEADC0DE)
#define FWINFO_ADDRESS              (ALIGNED((MAIN_APP_START_ADDRESS + sizeof(vector_table_t)), 16))
//#define FWINFO_VALIDATE_FROM        (FWINFO_ADDRESS + sizeof(firmware_info_t))
//#define VALIDATE_LENGTH(fw_length)  (fw_length - sizeof(vector_table_t) - sizeof(firmware_info_t))
#define SIGNATURE_ADDRESS           (FWINFO_ADDRESS + sizeof(firmware_info_t))

typedef struct firmware_info_t
{
    uint32_t sentinel;      //offset 0
    uint32_t device_id; //offset 4
    uint32_t firmware_version; //offset 8
    uint32_t length; //offset 12, length of the firmware image to be validated, starting from the vector table. This length is used by the bootloader to know how many bytes to read and validate before jumping to the main application.
   // uint32_t reserved[4]; 
   // uint32_t CRC32;
}firmware_info_t; //first page of application firmware must be reserved for this struct, and the vector table must be located before this struct, so that the bootloader can read the firmware info and validate the firmware before jumping to the main application.



#endif // INC_FIRMWARE_INFO_H