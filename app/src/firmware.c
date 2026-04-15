#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

#define LED_PORT (GPIOA)
#define LED_PIN  (GPIO5)

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

static void delay_cycles(uint32_t cycles) {
  for (uint32_t i = 0; i < cycles; i++) {
    __asm__("nop");
  }
}

int main(void) {
  rcc_setup();
  gpio_setup();

  while (1) {
    gpio_clear(LED_PORT, LED_PIN);
   // delay_cycles(84000000 / 4);
  }

  // Never return
  return 0;
}
