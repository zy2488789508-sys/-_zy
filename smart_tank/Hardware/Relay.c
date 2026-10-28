#include "stm32f10x.h"
#include "Relay.h"
RELAY1_INFO relay1_info = {0};
RELAY2_INFO relay2_info = {0};
RELAY3_INFO relay3_info = {0};
void Relay_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA|RCC_APB2Periph_AFIO , ENABLE);
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;				//设置为输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5|GPIO_Pin_6|GPIO_Pin_7;						//将初始化的Pin脚
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;				//可承载的最大频率
	GPIO_Init(GPIOA, &GPIO_InitStructure);							//初始化GPIO
	Relay1_Set(RELAY1_OFF);											//初始化完成后，关闭继电器1
	Relay2_Set(RELAY2_OFF);											//初始化完成后，关闭继电器2
	Relay3_Set(RELAY3_OFF);											//初始化完成后，关闭继电器3
	
}

void Relay1_Set(_Bool status)
{
	GPIO_WriteBit(GPIOA, GPIO_Pin_5, status == RELAY1_ON ? Bit_SET : Bit_RESET );		//如果status等于RELAY_ON，则返回Bit_SET，否则返回Bit_RESET
	relay1_info.Relay1_Status = status;
}

void Relay2_Set(_Bool status)
{
	GPIO_WriteBit(GPIOA, GPIO_Pin_6, status == RELAY2_ON ? Bit_SET : Bit_RESET );		//如果status等于RELAY_ON，则返回Bit_SET，否则返回Bit_RESET
	relay2_info.Relay2_Status = status;
}

void Relay3_Set(_Bool status)
{
	GPIO_WriteBit(GPIOA, GPIO_Pin_7, status == RELAY3_ON ? Bit_SET : Bit_RESET );		//如果status等于RELAY_ON，则返回Bit_SET，否则返回Bit_RESET
	relay3_info.Relay3_Status = status;
}

