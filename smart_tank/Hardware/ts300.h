#ifndef __TS300_H
#define __TS300_H
#include "stm32f10x.h"

void ts300b_init(void);
float ts300b_get_voltage(void);
float ts300b_get_turbidity(void);

#endif