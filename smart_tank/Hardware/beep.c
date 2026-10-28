#include "stm32f10x.h"

void beep_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void beep_on(void)
{
    GPIO_SetBits(GPIOA, GPIO_Pin_4);     // 高电平：三极管导通，蜂鸣器响
}

void beep_off(void)
{
    GPIO_ResetBits(GPIOA, GPIO_Pin_4);   // 低电平：三极管截止，蜂鸣器停
}