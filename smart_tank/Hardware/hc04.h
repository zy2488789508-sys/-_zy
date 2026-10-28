#ifndef __HC04_H
#define __HC04_H

#include "stm32f10x.h"

void hc04_init(void);
void time_init(void);
float hc04_get_distance(void);

#endif