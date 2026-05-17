#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/vector.h>
#include "common-defines.h"
#include <libopencm3/stm32/memorymap.h>
#include <libopencm3/cm3/scb.h>

#include "core/uart.h"
#include "core/firmware-info.h"
#include "core/system.h"
#include "comms.h"
#include "bl-flash.h"
#include "core/simple-timer.h"
#include "core/crc.h"
#include "core/firmware-info.h"

#define BOOTLOADER_START (0x08000000U) /* Start of flash memory*/

#define UART_PORT (GPIOA)
#define RX_PIN    (GPIO3)
#define TX_PIN    (GPIO2)

#define DEVICE_ID  (0x42)
#define SYNC_SEQ_0 (0xc4)
#define SYNC_SEQ_1 (0x55)
#define SYNC_SEQ_2 (0x7e)
#define SYNC_SEQ_3 (0x10)

#define DEFAULT_TIMEOUT_MS (5000) /* Default timeout for communication operations in milliseconds, can be adjusted as needed */

/*(State Machine) Step 2: Bootloader state machine */
typedef enum {
  BL_STATE_SYNC,
  BL_STATE_WAIT_FOR_FW_UPDATE_REQ,
  BL_STATE_DEVICE_ID_REQ,
  BL_STATE_DEVICE_ID_RES,
  BL_STATE_FW_LENGTH_REQ,
  BL_STATE_FW_LENGTH_RES,
  BL_STATE_ERASE_APPLICATION,
  BL_STATE_RECEIVE_FW,
  BL_STATE_DONE
}bl_state_t;

static bl_state_t bootloader_state = BL_STATE_SYNC; /* Initialize bootloader state to SYNC */
static uint32_t firmware_length = 0; /* Variable to store the length of the firmware being updated, marked as volatile since it may be modified in an interrupt or another context */
static uint32_t bytes_written = 0; /* Variable to track the number of bytes written to flash during the firmware update process, marked as volatile for the same reason as firmware_length */
static uint8_t sync_seq[4] = {0};
static simple_timer_t timer;
static comms_packet_t temp_packet;

//const uint8_t data[0x8000U] = {0};
static void bootloading_failed(void)
{
  comms_create_single_byte_packet(&temp_packet, BL_PACKET_NACK_DATA0);
  comms_send_packet(&temp_packet); /* Send a NACK packet if sync sequence is not observed within the timeout period */   
 bootloader_state = BL_STATE_DONE; /* Transition to DONE state after sending NACK, in a real application you might want to allow for multiple attempts or handle this differently */ 
}

static void check_for_timeout(void)
{
  if(simple_timer_has_elapsed(&timer))
  {
      bootloading_failed();
  }
}

static bool is_device_id_packet(const comms_packet_t* packet)
{
  if(packet-> length != 2)
    return false;
  if(packet->data[0] != BL_PACKET_DEVICE_ID_RES_DATA0)
    return false;
//  if(packet->data[1] != DEVICE_ID)
//    return false;
  for(uint8_t i = 2; i < PACKET_DATA_LENGTH; i++)
  {
    if(packet->data[i] != 0xFF) /* Ensure unused data bytes are set to 0xFF as expected for single byte packets */
      return false;
  }
  return true;
}

static bool is_fw_length_packet(const comms_packet_t* packet)
{
  if(packet-> length != 5)
    return false;
  if(packet->data[0] != BL_PACKET_FW_LENGTH_RES_DATA0)
    return false;
  for(uint8_t i = 5; i < PACKET_DATA_LENGTH; i++)
  {
    if(packet->data[i] != 0xFF) /* Ensure unused data bytes are set to 0xFF as expected for single byte packets */
      return false;
  }

  return true;
}

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

static void gpio_teardown(void)
{
  /* Reset UART pins to default state */
  gpio_mode_setup(UART_PORT, GPIO_MODE_ANALOG , GPIO_PUPD_NONE, TX_PIN | RX_PIN); /* Set UART pins to analog mode to reduce power consumption and avoid unintended behavior */
  rcc_periph_clock_disable(RCC_GPIOA); /* Disable GPIOA clock to save power, in a real application you might want to ensure that this does not affect other peripherals that may be using GPIOA */
}

static void jump_to_main_app(void)
{
  typedef void (*app_entry_t)(void); /* Define a function pointer type for the main application entry point */

  /*Reset vector table to main application start address*/
  uint32_t *reset_vector_entry = (uint32_t *)(MAIN_APP_START_ADDRESS + 4U); /* Reset vector is at offset 4 */
  uint32_t *reset_vector = (uint32_t *)*reset_vector_entry; /* Get the reset vector address from the main application */
    
  app_entry_t main_app_entry = (app_entry_t)reset_vector; /* Cast the reset vector to a function pointer */
  main_app_entry(); /* Jump to the main application */  
}

static bool validate_fw_image(void)
{
  firmware_info_t* firmware_info = (firmware_info_t*)FWINFO_ADDRESS; /* The firmware info struct is located at the start of the main application area in flash */

  if(firmware_info->sentinel != FIRMWARE_INFO_SENTINEL)
    return false; /* Check if the sentinel value is correct to validate that the firmware info struct is present and valid */

  if(firmware_info->device_id != DEVICE_ID)
    return false; /* Check if the device ID in the firmware info matches the expected device ID for this bootloader */

  const uint32_t* start_addr = (const uint32_t*)FWINFO_VALIDATE_FROM; /* The actual firmware data starts immediately after the firmware info struct */
  uint32_t computed_crc = crc32((const uint8_t*)start_addr, VALIDATE_LENGTH(firmware_info->length)); /* Compute the CRC32 of the firmware data using the length specified in the firmware info struct */
  if(computed_crc != firmware_info->CRC32)
    return false; /* Check if the computed CRC matches the CRC stored in the firmware info struct to validate the integrity of the firmware */
  
  return true; /* If all checks pass, the firmware image is considered valid */
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
  #if 0 /*Testing Flash API */
  system_setup();
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
  #if 0 /*Testing Simple Timers API */
  system_setup();
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
  #endif


  system_setup();
  gpio_setup();
  uart_setup();
  comms_setup();

  simple_timer_init(&timer, DEFAULT_TIMEOUT_MS, false); /* Initialize a one-shot timer with a wait time of 5000 time units */

  while(bootloader_state != BL_STATE_DONE)
  { 
    if(bootloader_state == BL_STATE_SYNC)
    {
      if(uart_data_available())
      {
        sync_seq[0] = sync_seq[1];
        sync_seq[1] = sync_seq[2];
        sync_seq[2] = sync_seq[3];
        sync_seq[3] = uart_read_byte();

        bool is_match = sync_seq[0] == SYNC_SEQ_0;
        is_match = is_match && (sync_seq[1] == SYNC_SEQ_1);
        is_match = is_match && (sync_seq[2] == SYNC_SEQ_2);
        is_match = is_match && (sync_seq[3] == SYNC_SEQ_3);
        if(is_match)
        {
          comms_create_single_byte_packet(&temp_packet, BL_PACKET_SYNC_OBSERVED_DATA0);
          comms_send_packet(&temp_packet);
          bootloader_state = BL_STATE_WAIT_FOR_FW_UPDATE_REQ;
          simple_timer_reset(&timer); /* Reset the timer when the sync sequence is observed to start the timeout for the next expected event (e.g., firmware update request) */
        }
        else
        {
          check_for_timeout(); /* Check for timeout if sync sequence is not observed within the expected time frame */
        }
      }
      else
      {
        check_for_timeout(); /* Check for timeout if no data is available, this will handle the case where the sync sequence is not observed within the expected time frame */
      }     
      continue; /* Continue to the next iteration of the loop to keep checking for sync sequence and handling timeouts */      
    }
    comms_update(); /* Update communication state machine to handle incoming packets */
    
    switch(bootloader_state)
    {
      case BL_STATE_WAIT_FOR_FW_UPDATE_REQ:
      {
        if(comms_packet_available())
        {
          comms_receive_packet(&temp_packet); /* read - Receive the incoming packet, in a real application you would want to check the packet contents to determine if it's a firmware update request and handle it accordingly */
          if(comms_is_single_byte_packet(&temp_packet, BL_PACKET_FW_UPDATE_REQ_DATA0))
          {
            /* Transition to the next state to handle device ID request/response */
            comms_create_single_byte_packet(&temp_packet, BL_PACKET_FW_UPDATE_RES_DATA0);
            comms_send_packet(&temp_packet); /* Send a response packet indicating that the firmware update request was received, in a real application you might want to include additional information or perform other actions here */
            bootloader_state = BL_STATE_DEVICE_ID_REQ; /* Transition to the next state to handle device ID request/response */
            simple_timer_reset(&timer); 
          }
          else
          {
            /* Handle unexpected packet or ignore */
            bootloading_failed();
          }
          
        }
        else
        {
          check_for_timeout(); /* Check for timeout if firmware update request is not received within the expected time frame */
        }

      }break;
      case BL_STATE_DEVICE_ID_REQ:
      {
        /* Transition to the next state to handle device ID request/response */
        comms_create_single_byte_packet(&temp_packet, BL_PACKET_DEVICE_ID_REQ_DATA0);
        comms_send_packet(&temp_packet); /* Send a response packet indicating that the firmware update request was received, in a real application you might want to include additional information or perform other actions here */
        bootloader_state = BL_STATE_DEVICE_ID_RES; /* Transition to the next state to handle device ID request/response */
        simple_timer_reset(&timer); /* Reset the timer when the device ID request is sent to start the timeout for the next expected event (e.g., device ID response) */
      }break;
      case BL_STATE_DEVICE_ID_RES:
      {
          if(comms_packet_available())
          {
            comms_receive_packet(&temp_packet); /* read - Receive the incoming packet, in a real application you would want to check the packet contents to determine if it's a firmware update request and handle it accordingly */
            if(is_device_id_packet(&temp_packet) && (temp_packet.data[1] == DEVICE_ID)) /* Check if the received packet is a valid device ID response packet and if the device ID matches the expected value */ 
            {
              simple_timer_reset(&timer); /* Reset the timer when the device ID response is received to start the timeout for the next expected event (e.g., firmware length request) */
              bootloader_state = BL_STATE_FW_LENGTH_REQ; /* Transition to the next state to handle firmware length request/response */
            }
            else
            {
              /* Handle unexpected packet or ignore */
              bootloading_failed();
            }
            
          }
          else
          {
            check_for_timeout(); /* Check for timeout if device ID response is not received within the expected time frame */
          }
        
      }break;
      case BL_STATE_FW_LENGTH_REQ:
      {
        simple_timer_reset(&timer); 
        comms_create_single_byte_packet(&temp_packet, BL_PACKET_FW_LENGTH_REQ_DATA0);
        comms_send_packet(&temp_packet); /* Send a request packet to get the firmware length */
        bootloader_state = BL_STATE_FW_LENGTH_RES; /* Transition to the next state to handle firmware length response */
      }break;
      case BL_STATE_FW_LENGTH_RES:
      {
        if(comms_packet_available())
        {
          comms_receive_packet(&temp_packet); /* read - Receive the incoming packet, in a real application you would want to check the packet contents to determine if it's a firmware length response and handle it accordingly */
          if(is_fw_length_packet(&temp_packet)) /* Check if the received packet is a valid firmware length response packet */ 
          {
            /* Extract firmware length from the packet data, assuming it's sent in bytes 1-4 of the data array in little-endian format */
            firmware_length = (temp_packet.data[1]) | (temp_packet.data[2] << 8) | (temp_packet.data[3] << 16) | (temp_packet.data[4] << 24);
            if(firmware_length <= MAX_FW_LENGTH) /* Check if the received firmware length fits within the main application area, this is a safety check to prevent writing beyond the allocated flash memory */
            {
                simple_timer_reset(&timer); /* Reset the timer when the firmware length response is received to start the timeout for the next expected event (e.g., ready for data) */
              bootloader_state = BL_STATE_ERASE_APPLICATION; /* Transition to the next state to handle erasing the application area before receiving firmware data */

            }
            else
            {
              /* Handle invalid firmware length, for example by sending a NACK or transitioning to an error state */
              bootloading_failed();
            }
          }
          else
          {
            /* Handle unexpected packet or ignore */
            bootloading_failed();
          } 
        }
        else
        {
          check_for_timeout(); /* Check for timeout if firmware length response is not received within the expected time frame */
        }
      }
      break;
      case BL_STATE_ERASE_APPLICATION:
      {
        bl_flash_erase_main_application(); /* Erase the flash memory area allocated for the main application, this is typically done before writing new firmware to ensure that the flash memory is in a known state */
        simple_timer_reset(&timer); /* Reset the timer when the ready for data packet is sent to start the timeout for receiving firmware data */
        comms_create_single_byte_packet(&temp_packet, BL_PACKET_READY_FOR_DATA_DATA0);
        comms_send_packet(&temp_packet); /* Send a packet indicating that the bootloader is ready to receive firmware data, in a real application you might want to include additional information or perform other actions here */
        bootloader_state = BL_STATE_RECEIVE_FW; /* Transition to the next state to handle receiving firmware data */
        
      
      }break;
      case BL_STATE_RECEIVE_FW:
      {
        if(comms_packet_available())
        {
          comms_receive_packet(&temp_packet); /* read - Receive the incoming packet, in a real application you would want to check the packet contents to determine if it's a firmware data packet and handle it accordingly */
          if(temp_packet.length > 0 && temp_packet.length <= PACKET_DATA_LENGTH) /* Check if the received packet has a valid length for firmware data */ 
          {
            /* Write the received firmware data to flash memory, in a real application you would want to keep track of the total bytes written and ensure that you don't write beyond the allocated flash memory for the main application */
            bl_flash_write(MAIN_APP_START_ADDRESS + bytes_written, temp_packet.data, (temp_packet.length & 0x0F)+1); /* Write the received firmware data to flash memory at the appropriate address based on how many bytes have been written so far */
            bytes_written += (temp_packet.length & 0x0F)+1; /* Update the count of bytes written with the length of the received packet */
            simple_timer_reset(&timer); /* Reset the timer each time a valid firmware data packet is received to prevent timeout while waiting for the next packet */
            if(bytes_written >= firmware_length) /* Check if we have received and written all the expected firmware data based on the firmware length that was previously communicated, this is a condition to determine when we are done receiving firmware data */
            {
              comms_create_single_byte_packet(&temp_packet, BL_PACKET_UPDATE_SUCCESSFUL_DATA0);
              comms_send_packet(&temp_packet); /* Send a packet indicating that the firmware update was successful, in a real application you might want to include additional information or perform other actions here */
              bootloader_state = BL_STATE_DONE; /* Transition to DONE state after successfully receiving and writing all firmware data */
            }
            else
            {
              comms_create_single_byte_packet(&temp_packet, BL_PACKET_READY_FOR_DATA_DATA0);
              comms_send_packet(&temp_packet); /* Send an acknowledgment packet for the received firmware data packet, in a real application you might want to include additional information or perform other actions here */
            }
          }
          else
          {
            /* Handle invalid packet length, for example by sending a NACK or ignoring the packet */
            bootloading_failed();
          }
        }
        else
        {
          check_for_timeout(); /* Check for timeout if expected firmware data packets are not received within the expected time frame during the firmware reception process */
        }
      }break;
      default:
      {
        bootloader_state = BL_STATE_SYNC; /* Transition to SYNC state for any unexpected state, in a real application you might want to handle this differently, such as by resetting the bootloader or entering an error state */
      }break;
    }


  
  }
  /*ToDo : Teardown peripherals and system before jumping to main app */
  delay_cycles(100000); /* Small delay to ensure that the last communication packet is sent before we teardown peripherals and jump to the main application, in a real application you might want to implement a more robust way to ensure that all communication is complete before proceeding */
  uart_teardown(); /* Teardown UART peripheral to ensure it's in a clean state before jumping to the main application, in a real application you would want to ensure that this does not affect any peripherals that the main application may be using or that the main application properly reinitializes any peripherals it needs */
  gpio_teardown(); /* Teardown GPIO configuration to ensure it's in a clean state before jumping to the main application, in a real application you would want to ensure that this does not affect any peripherals that the main application may be using or that the main application properly reinitializes any GPIO configuration it needs */
  system_teardown(); /* Teardown system configuration to ensure it's in a clean state before jumping to the main application, in a real application you would want to ensure that this does not affect the main application or that the main application properly reinitializes any system configuration it needs */

  if(validate_fw_image())
  {
    jump_to_main_app(); /* Jump to the main application */
  }
  else{
    scb_reset_core();
  }

  
  return 0;  
}

