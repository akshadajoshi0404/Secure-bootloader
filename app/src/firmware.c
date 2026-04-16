#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/vector.h>


#define LED_PORT (GPIOA)
#define LED_PIN  (GPIO5)

#define CPU_FREQ 84000000
#define SYSTICK_FREQ 1000

static void rcc_setup(void) {
  rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_3V3_84MHZ]);
}

static void gpio_setup(void) {
  rcc_periph_clock_enable(RCC_GPIOA);
  /*GPIO - LED ON
  Pull up and pull down - This is used to set the internal pull-up or pull-down resistors for the GPIO pin. In this case,
  GPIO_PUPD_NONE means that no internal pull-up or pull-down resistors are enabled. This is often used when the pin is driven 
  by an external circuit that already has the necessary pull-up or pull-down resistors, or when the pin is configured 
  as an output and does not require any pull-up or pull-down resistors. [This register keeps the pin in a high-impedance 
  state when not driven, which can help reduce power consumption and prevent unintended behavior.]
  */
  gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_PIN);
}

static void systick_setup(void)
{
  systick_set_frequency(SYSTICK_FREQ, CPU_FREQ); /* Set systick to trigger every 1ms */
  systick_counter_enable(); /* Start the systick counter */
  systick_interrupt_enable(); /* Enable systick interrupt */
}
volatile uint32_t tick_count = 0;
void sys_tick_handler(void)
{
  tick_count++;
}

static uint64_t get_ticks(void) {
  return tick_count;
}

#if 0 /*this function creates busy-wait delay*/
static void delay_cycles(uint32_t cycles) { /*Using this function was consuming CPU cycles alternate is doing it with systick - pheriphersl will time keeping and CPU is free */
  for (uint32_t i = 0; i < cycles; i++) {
    __asm__("nop");
  }
}
#endif

int main(void) {
  rcc_setup();
  gpio_setup();
  systick_setup();

  uint64_t last_tick = get_ticks();
  while (1) {

    if (get_ticks() - last_tick >= 500) 
    { /* Toggle LED every 2 seconds */
      last_tick = get_ticks();
      gpio_toggle(LED_PORT, LED_PIN);
    }
  }

  // Never return
  return 0;
}
