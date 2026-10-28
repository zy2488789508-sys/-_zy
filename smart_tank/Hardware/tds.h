#ifndef __TDS_H
#define __TDS_H

#include "stm32f10x.h"

void  TDS_Init(void);
float TDS_ReadVoltage(void);               // 专用于调试读取电压
float TDS_ReadValue(float temperature);    // 读取TDS值(ppm)

#endif