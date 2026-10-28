#include "stm32f10x.h"
#include "delay.h"
#include "light.h"

#define LIGHT_GPIO	GPIOC
#define LIGHT_PIN	GPIO_Pin_13

static uint8_t Light_Status = 0;		//0=灭 1=亮

void Light_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = LIGHT_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(LIGHT_GPIO, &GPIO_InitStructure);
	Light_SetState(0);					//上电默认关灯
}

void Light_on(void)
{
	GPIO_SetBits(LIGHT_GPIO, LIGHT_PIN);
	Light_Status = 1;
}

void Light_off(void)
{
	GPIO_ResetBits(LIGHT_GPIO, LIGHT_PIN);
	Light_Status = 0;
}

void Light_Toggle(void)
{
	if(Light_Status)	Light_off();
	else				Light_on();
}

void Light_SetState(uint8_t on)
{
	if(on)	Light_on();
	else	Light_off();
}

uint8_t Light_GetState(void)
{
	return Light_Status;
}
