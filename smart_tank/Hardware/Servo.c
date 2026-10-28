#include "stm32f10x.h"                  // Device header
#include "PWM.h"
#include "Servo.h"

/* 舵机当前角度（0~90 度），供平台读取和限幅使用 */
static float Servo_Angle = 0.0f;

void Servo_Init(void)
{
	PWM_Init();
	Servo_SetAngleEx(0.0f);				//上电归零
}

void Servo_SetAngle(float Angle)
{
	PWM_SetCompare1(Angle/180*2000+500);
}

/* 带限幅和状态记忆的角度设置：平台下发、按键都用它 */
void Servo_SetAngleEx(float Angle)
{
	if(Angle < 0.0f)	Angle = 0.0f;
	if(Angle > 90.0f)	Angle = 90.0f;

	Servo_Angle = Angle;
	Servo_SetAngle(Angle);
}

/* 读取当前角度 */
float Servo_GetAngle(void)
{
	return Servo_Angle;
}
