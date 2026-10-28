//单片机头文件
#include "stm32f10x.h"

//硬件驱动
#include "Key.h"
#include "Delay.h"
#include "Relay.h"
#include "Servo.h"
#include "light.h"


/*
************************************************************
*	函数名称：	KEY_EXTI_Init
*
*	函数功能：	按键外部中断初始化（PB7、PB12、PB13、PB14、PB15）
*
*	入口参数：	无
*
*	返回参数：	无
*
*	说明：		PB7  -> EXTI7 （EXTI9_5  中断），控制照明灯亮灭
*				PB12 -> EXTI12（EXTI15_10中断），控制继电器3
*				PB13 -> EXTI13（EXTI15_10中断），控制继电器2
*				PB14 -> EXTI14（EXTI15_10中断），控制继电器1
*				PB15 -> EXTI15（EXTI15_10中断），控制舵机
************************************************************
*/
void KEY_EXTI_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 开启GPIOB和AFIO时钟（外部中断要用AFIO）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    // 配置PB7、12、13、14、15为上拉输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 将PB7连接到EXTI7，下降沿触发：控制照明灯亮灭
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource7);
    EXTI_InitStructure.EXTI_Line = EXTI_Line7;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    // 将PB12连接到EXTI12
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource12);
    EXTI_InitStructure.EXTI_Line = EXTI_Line12;
    EXTI_Init(&EXTI_InitStructure);

    // 将PB13连接到EXTI13
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource13);
    EXTI_InitStructure.EXTI_Line = EXTI_Line13;
    EXTI_Init(&EXTI_InitStructure);

    // 将PB14连接到EXTI14
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource14);
    EXTI_InitStructure.EXTI_Line = EXTI_Line14;
    EXTI_Init(&EXTI_InitStructure);

    // 将PB15连接到EXTI15
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource15);
    EXTI_InitStructure.EXTI_Line = EXTI_Line15;
    EXTI_Init(&EXTI_InitStructure);

    // 配置NVIC：EXTI15_10（PB12~PB15）
    NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 配置NVIC：EXTI9_5（PB7）
    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/*
************************************************************
*	函数名称：	EXTI15_10_IRQHandler
*
*	函数功能：	PB12~PB15 按键中断：继电器、舵机
*
*	入口参数：	无
*
*	返回参数：	无
*
*	说明：		下降沿触发，10ms 消抖后执行对应动作
************************************************************
*/
void EXTI15_10_IRQHandler(void)
{
    DelayXms(10);
    if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_12) ==0)
    {
        /* PB12 -> 继电器3(PA7) -> 增氧泵 */
        if(relay3_info.Relay3_Status == RELAY3_ON)
        {
            Relay3_Set(RELAY3_OFF);
        }
        else	Relay3_Set(RELAY3_ON);
    }
    EXTI_ClearITPendingBit(EXTI_Line12);

    if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_13) ==0)
    {
        /* PB13 -> 继电器2(PA6) -> 抽水泵 */
        if(relay2_info.Relay2_Status == RELAY2_ON)
        {
            Relay2_Set(RELAY2_OFF);
        }
        else	Relay2_Set(RELAY2_ON);
    }
    EXTI_ClearITPendingBit(EXTI_Line13);

    if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_14) ==0)
    {
        /* PB14 -> 继电器1(PA5) -> 放水泵 */
        if(relay1_info.Relay1_Status == RELAY1_ON)
        {
            Relay1_Set(RELAY1_OFF);
        }
        else	Relay1_Set(RELAY1_ON);
    }
    EXTI_ClearITPendingBit(EXTI_Line14);

    if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_15) ==0)
    {
        float a = Servo_GetAngle() + 90.0f;
        if (a > 90.0f)
        {
            a = 0.0f;
        }
        Servo_SetAngleEx(a);
    }
    EXTI_ClearITPendingBit(EXTI_Line15);
}

/*
************************************************************
*	函数名称：	EXTI9_5_IRQHandler
*
*	函数功能：	PB7 按键中断：控制照明灯的亮灭
*
*	入口参数：	无
*
*	返回参数：	无
*
*	说明：		每按一次 PB7（下降沿）翻转一次灯光状态，10ms 消抖
************************************************************
*/
void EXTI9_5_IRQHandler(void)
{
    if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_7) == 0)
    {
        DelayXms(10);							// 消抖
        if(GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_7) == 0)
        {
            Light_Toggle();						// 翻转照明灯（亮 / 灭）
        }
    }
    EXTI_ClearITPendingBit(EXTI_Line7);
}
