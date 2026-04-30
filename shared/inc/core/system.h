#ifndef SYSTEM_H
#define SYSTEM_H

#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/vector.h>
#include <libopencm3/stm32/rcc.h>
#include <common-defines.h>

#define CPU_FREQ       (84000000)
#define SYSTICK_FREQ    (1000)

void system_setup(void);
uint64_t system_get_ticks(void);
void delay_cycles(uint32_t cycles);

#endif // SYSTEM_H