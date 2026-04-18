#include "common-defines.h"
#include <libopencm3/stm32/memorymap.h>

#define BOOTLOADER_SIZE (0x8000U) /* 32 KB */
#define BOOTLOADER_START (0x08000000U) /* Start of flash memory*/
#define MAIN_APPL_START_ADDR (FLASH_BASE + BOOTLOADER_SIZE) /* Start address of main application */

//const uint8_t data[0x8000U] = {0};

static void jump_to_main_app(void)
{
  typedef void (*app_entry_t)(void); /* Define a function pointer type for the main application entry point */

  /*Reset vector table to main application start address*/
  uint32_t *reset_vector_entry = (uint32_t *)(MAIN_APPL_START_ADDR + 4U); /* Reset vector is at offset 4 */
  uint32_t *reset_vector = (uint32_t *)*reset_vector_entry; /* Get the reset vector address from the main application */
    
  app_entry_t main_app_entry = (app_entry_t)reset_vector; /* Cast the reset vector to a function pointer */
  main_app_entry(); /* Jump to the main application */  
}


int main(void) {
  /* In a real bootloader, you would typically perform tasks such as:
     - Checking for a valid application in the main application area
     - Verifying the integrity of the application (e.g., using checksums or signatures)
     - Optionally providing a way to update the firmware (e.g., via UART, USB, etc.)
     For this example, we will simply jump to the main application. */

  #if 0 /*prevent linking bootloader beyond 32kb testing */  
  volatile uint8_t x = 0;
  for(uint32_t i = 0; i < sizeof(data); i++) {
    x += data[i]; /*should fail compilation if bootloader exceeds 32kb */
  }
  #endif 

  jump_to_main_app(); /* Jump to the main application */
  // Never return
  return 0;
}

