#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"

// 函数声明
void KEY_EXTI_Init(void);   // 按键外部中断初始化：PB7 控制照明灯，PB12~PB15 控制继电器/舵机

#endif /* __KEY_H */
