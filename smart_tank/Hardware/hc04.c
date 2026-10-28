#include "stm32f10x.h"
#include "Delay.h"
#define TRIG_PIN   GPIO_Pin_10
#define TRIG_PORT  GPIOB
#define ECHO_PIN   GPIO_Pin_11
#define ECHO_PORT  GPIOB
void hc04_init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = TRIG_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = ECHO_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_InitStructure);
	
	GPIO_ResetBits(TRIG_PORT,TRIG_PIN);
}
void time_init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_InternalClockConfig(TIM2); // 选择内部时钟（标准流程，建议加上）
    
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1; // 时钟分割
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // 向上计数模式
    TIM_TimeBaseStructure.TIM_Period = 0xFFFF; // 自动重装载值（防止溢出，65535us足够测距）
    TIM_TimeBaseStructure.TIM_Prescaler = 72 - 1; // 预分频，72MHz/72 = 1MHz，即1us加1
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0; // 高级定时器才用，普通定时器给0
    
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    TIM_Cmd(TIM2, ENABLE); // 使能定时器
}

// HC-SR04 测距函数，返回距离(cm)，-1表示超时无回波
float hc04_get_distance(void)
{
    uint32_t duration = 0;

    // 1. 发送 10us 触发脉冲
    GPIO_SetBits(TRIG_PORT, TRIG_PIN);
    DelayUs(10);
    GPIO_ResetBits(TRIG_PORT, TRIG_PIN);

    // 2. 等待 ECHO 变高，超时时间 30ms（软件计数兜底，防止定时器异常时死等）
    TIM_SetCounter(TIM2, 0);
    uint32_t guard = 0;
    while (GPIO_ReadInputDataBit(ECHO_PORT, ECHO_PIN) == Bit_RESET)
    {
        if (++guard > 500000) return -1.0f;
        if (TIM_GetCounter(TIM2) > 30000) return -1.0f; // 超时
    }

    // 3. 测量 ECHO 高电平持续时间（同样加软件兜底）
    TIM_SetCounter(TIM2, 0);
    guard = 0;
    while (GPIO_ReadInputDataBit(ECHO_PORT, ECHO_PIN) == Bit_SET)
    {
        if (++guard > 500000) return -1.0f;
        if (TIM_GetCounter(TIM2) > 30000) return -1.0f; // 超时
    }
    duration = TIM_GetCounter(TIM2);

    // 4. 计算距离 (声速340m/s，除以2是因为往返)
    return duration / 58.0f;
}