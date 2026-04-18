#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include "core/system.h"
#include "core/timer.h"
#include "libopencm3/cm3/scb.h" /*System control block 1. Register Definitions (Memory-Mapped I/O) 
The header defines memory-mapped registers (MMIO32) for the SCB, enabling control over the processor core: Vector Table and many more*/

#define BOOTLOADR_SIZE 0x8000 /* 32 KB bootloader size */

#define LED_PORT (GPIOA)
#define LED_PIN  (GPIO5)


static void vector_setup(void) {
  /* Set the vector table base address to the start of the bootloader */
  SCB_VTOR = BOOTLOADR_SIZE; /* Set the Vector Table Offset Register (VTOR) to the start of the bootloader */
}

static void gpio_setup(void) {
  rcc_periph_clock_enable(RCC_GPIOA);
  #if 0 
  /*GPIO - LED ON
  Pull up and pull down - This is used to set the internal pull-up or pull-down resistors for the GPIO pin. In this case,
  GPIO_PUPD_NONE means that no internal pull-up or pull-down resistors are enabled. This is often used when the pin is driven 
  by an external circuit that already has the necessary pull-up or pull-down resistors, or when the pin is configured 
  as an output and does not require any pull-up or pull-down resistors. [This register keeps the pin in a high-impedance 
  state when not driven, which can help reduce power consumption and prevent unintended behavior.]
  */
  gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_PIN);
  #endif

  //GPIO Setup with Alternate function - PWM output
  gpio_mode_setup(LED_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, LED_PIN);
  gpio_set_af(LED_PORT, GPIO_AF1, LED_PIN); /* Set alternate function for LED pin to AF1 (TIM2) */
  
}


int main(void) {
  vector_setup(); /* Set the vector table base address to the start of the bootloader */
  system_setup();
  gpio_setup();
  timer_setup();

  float duty_cycle = 0.0f;
  timer_pwm_set_duty_cycle(duty_cycle); /* Set initial duty cycle to 0% */
  uint64_t last_tick = system_get_ticks();
  while (1) 
  {
    if (system_get_ticks() - last_tick >= 10) 
    { /* Toggle LED every 10 ms */
      duty_cycle += 1.0f; /* Increase duty cycle by 1% every 10 ms */
      if (duty_cycle > 100.0f) {
        duty_cycle = 0.0f; /* Reset duty cycle to 0% when it exceeds 100% */
      }
      timer_pwm_set_duty_cycle(duty_cycle); /* Update duty cycle */ 
      last_tick = system_get_ticks();
     // gpio_toggle(LED_PORT, LED_PIN);
    }
  }

  // Never return
  return 0;
}

