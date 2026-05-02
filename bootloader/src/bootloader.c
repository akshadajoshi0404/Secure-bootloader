#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/vector.h>
#include "common-defines.h"
#include <libopencm3/stm32/memorymap.h>

#include "core/uart.h"
#include "core/system.h"
#include "comms.h"
#include "bl-flash.h"
#include "core/simple-timer.h"

#define BOOTLOADER_SIZE (0x8000U) /* 32 KB */
#define BOOTLOADER_START (0x08000000U) /* Start of flash memory*/
#define MAIN_APPL_START_ADDR (FLASH_BASE + BOOTLOADER_SIZE) /* Start address of main application */

#define UART_PORT (GPIOA)
#define RX_PIN    (GPIO3)
#define TX_PIN    (GPIO2)

//const uint8_t data[0x8000U] = {0};

static void gpio_setup(void)
{
  /* Enable GPIOA clock */
  rcc_periph_clock_enable(RCC_GPIOA);

  /* Set TX pin as output and RX pin as input */
  gpio_mode_setup(UART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TX_PIN);
  gpio_mode_setup(UART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, RX_PIN);

  /* Set alternate function for UART pins (AF7 for USART2) */
  gpio_set_af(UART_PORT, GPIO_AF7, TX_PIN | RX_PIN);
}

static void jump_to_main_app(void)
{
  typedef void (*app_entry_t)(void); /* Define a function pointer type for the main application entry point */

  /*Reset vector table to main application start address*/
  uint32_t *reset_vector_entry = (uint32_t *)(MAIN_APPL_START_ADDR + 4U); /* Reset vector is at offset 4 */
  uint32_t *reset_vector = (uint32_t *)*reset_vector_entry; /* Get the reset vector address from the main application */
    
  app_entry_t main_app_entry = (app_entry_t)reset_vector; /* Cast the reset vector to a function pointer */
  main_app_entry(); /* Jump to the main application */  
}


int main(void) 
{
  #if 0 
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
  system_setup(); /* Set up system clock and peripherals if needed */
  gpio_setup(); /* Set up GPIOs for UART communication if needed */
  uart_setup(); /* Set up the ART accelerator for flash access if needed */
  comms_setup(); /* Set up communication interfaces (e.g., UART) if needed */
  
  //1 Testing 
  comms_packet_t packet =
  {
    .length = 10,
    .data = {0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x07,0x08,0x09,0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    .crc = 0
  };
  packet.crc = comms_calculate_crc(&packet); /* Compute CRC for the packet data */
  packet.crc++; /* Intentionally corrupt CRC for testing */
  #endif

  system_setup();
  #if 0 /*Testing Flash*/
  uint8_t data[1024] = {0}; /* Example data to write to flash, in a real scenario this would come from the communication interface */
  for(uint16_t i = 0; i < sizeof(data); i++)
    data[i] = i & 0xff; /* Fill data with some pattern for testing */

  bl_flash_erase_main_application(); /* Erase the flash memory area for the main application */
  bl_flash_write( 0x08008000, data, sizeof(data)); /* Write the example data to the main application area in flash */
  bl_flash_write( 0x0800C000, data, sizeof(data)); /* Write the example data to the main application area in flash */
  bl_flash_write( 0x08010000, data, sizeof(data)); /* Write the example data to the main application area in flash */
  bl_flash_write( 0x08020000, data, sizeof(data)); /* Write the example data to the main application area in flash */
  bl_flash_write( 0x08040000, data, sizeof(data)); /* Write the example data to the main application area in flash */
  bl_flash_write( 0x08060000, data, sizeof(data)); /* Write the example data to the main application area in flash */
  #endif

  simple_timer_t timer;
  simple_timer_t timer2;

  simple_timer_init(&timer, 1000, false); /* Initialize a one-shot timer with a wait time of 1000 time units */
  simple_timer_init(&timer2, 2000, true);
  while(true)
  {
    #if 0 /*Testing Comms*/
    comms_update(); /* Update communication state machine to handle incoming packets */
    comms_send_packet(&packet); /* Send the packet over the communication interface */
    delay_cycles(500); /* Simulate some delay for testing purposes */
    #endif

    if(simple_timer_has_elapsed(&timer)) 
    {
        volatile uint32_t x = 0; /* Prevent compiler optimization for testing */
        x++ ; /* Increment x each time the timer elapses, in a real application this could be used for periodic tasks */
    }

    if(simple_timer_has_elapsed(&timer2)) 
    {
      simple_timer_reset(&timer); // Reset the one-shot timer when the periodic timer elapses, in a real application this could be used to trigger periodic events or tasks
    }

  }
  /*ToDo : Teardown peripherals and system before jumping to main app */

  jump_to_main_app(); /* Jump to the main application */
  return 0;
}

