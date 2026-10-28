#ifndef __LIGHT_H
#define __LIGHT_H

#include "stm32f10x.h"

void Light_init(void);				// 照明灯初始化（PC13 推挽输出，上电默认灭）
void Light_on(void);				// 开灯
void Light_off(void);				// 关灯
void Light_Toggle(void);			// 翻转灯状态
void Light_SetState(uint8_t on);	// 指定状态：0=灭 1=亮
uint8_t Light_GetState(void);		// 读当前状态：0=灭 1=亮

#endif
