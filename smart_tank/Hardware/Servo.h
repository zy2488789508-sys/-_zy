#ifndef __SERVO_H
#define __SERVO_H

void  Servo_Init(void);
void  Servo_SetAngle(float Angle);		// 直接设置角度（内部接口）
void  Servo_SetAngleEx(float Angle);	// 带0~90限幅并记忆状态（平台/按键用）
float Servo_GetAngle(void);				// 读取当前角度

#endif
