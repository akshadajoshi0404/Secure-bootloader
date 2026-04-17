#include "core/timer.h"
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>

//sys freq = 84_000_000
//timer_freq = sys_freq / (prescaler-1 * period-1)

#define PRESCALER   (84)
#define ARR_VALUE        (1000)

void timer_setup(void)
{
    rcc_periph_clock_enable(RCC_TIM2); /* Enable clock for TIM2 */
    timer_set_mode(TIM2, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP); /* Set timer mode */
    timer_set_oc_mode(TIM2, TIM_OC1, TIM_OCM_PWM1); /* Set output compare mode to PWM1 */
    timer_enable_counter(TIM2); /* Start the timer */
    timer_enable_oc_output(TIM2, TIM_OC1); /* Enable output for channel 1 */
    timer_set_prescaler(TIM2, PRESCALER - 1); /* Set prescaler */
    timer_set_period(TIM2, ARR_VALUE - 1); /* Set auto-reload value */
}

void timer_pwm_set_duty_cycle(float duty_cycle)
{
    //duty cycle should be between 0 and 100
    //duty cycle = CCR/ARR * 100
    //CCR = duty cycle * ARR / 100
    if (duty_cycle < 0.0f) {
        duty_cycle = 0.0f;
    } else if (duty_cycle > 100.0f) {
        duty_cycle = 100.0f;
    }
    uint32_t pulse_length = (uint32_t)((duty_cycle / 100.0f) * ARR_VALUE); /* Calculate pulse length based on duty cycle */
    timer_set_oc_value(TIM2, TIM_OC1, pulse_length); /* Set the pulse length for channel 1 */
}
