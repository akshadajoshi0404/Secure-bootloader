#ifndef TIMER_H
#define TIMER_H

#include "common-defines.h"

void timer_setup(void);
void timer_pwm_set_duty_cycle(float duty_cycle); /*duty cycle in percentage*/

#endif  // TIMER_H
