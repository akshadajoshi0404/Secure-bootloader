#include "core/system.h"

static void rcc_setup(void) {
  rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_3V3_84MHZ]);
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

uint64_t system_get_ticks(void) {
  return tick_count;
}

void system_setup(void) {
  rcc_setup();
  systick_setup();
}

void system_teardown(void) {
  /* In a real application, you would typically disable peripherals, interrupts, and perform any necessary cleanup before jumping to the main application. For this example, we will leave this function empty. */
  systick_interrupt_disable(); /* Disable systick interrupt */
  systick_counter_disable(); /* Stop the systick counter */ 
  systick_clear(); /* Clear systick counter and interrupt flag */
  /* You might also want to disable other peripherals or interrupts here as needed */
}

#if 1 /*this function creates busy-wait delay*/ 
void delay_cycles(uint32_t cycles) { /*Using this function was consuming CPU cycles alternate is doing it with systick - pheriphersl will time keeping and CPU is free */
  for (uint32_t i = 0; i < cycles; i++) {
    __asm__("nop");
  }
}
#endif
