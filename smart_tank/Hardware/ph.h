#ifndef __PH_H
#define __PH_H

#include "stm32f10x.h"

void  PH_Init(void);
float PH_ReadValue(void);
void  PH_Calibrate(float v1, float ph1, float v2, float ph2); // 两点校准

#endif