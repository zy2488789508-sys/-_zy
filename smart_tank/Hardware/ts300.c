#include "ts300.h"

// 初始化 ADC1 和 PA1
void ts300b_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    // 1. 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6); // ADC 时钟 = 72MHz / 6 = 12MHz (不能超过14MHz)

    // 2. 配置 PA1 为模拟输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN; // 模拟输入
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 配置 ADC1 为独立模式、单次转换
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE; // 单次转换
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    // 4. 配置 ADC 通道1（对应PA1），采样时间 55.5 周期
    // 【关键修改在这里】：ADC_Channel_0 改成了 ADC_Channel_1
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 1, ADC_SampleTime_55Cycles5);

    // 5. 使能 ADC 并校准
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

// 读取一次 ADC 值并换算为电压 (单位：V)
float ts300b_get_voltage(void)
{
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    { uint16_t g = 0; while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) { if(++g > 20000) break; } }

    uint16_t adc_value = ADC_GetConversionValue(ADC1);
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);

    // STM32 的 ADC 是 12 位 (0~4095)，参考电压 3.3V
    return (adc_value / 4095.0f) * 3.3f;
}

// 将电压换算为浑浊度值 (NTU，近似值)
float ts300b_get_turbidity(void)
{
    float voltage = ts300b_get_voltage();

    // ================= 校准参数 =================
    #define V_CLEAN   1.980f    // 清水电压
    #define V_DIRTY   1.000f    // 浊水电压（建议后续根据实测修改）
    #define MAX_NTU   1000.0f 
    // ============================================

    // 计算 NTU (电压越高，NTU越高)
    float ntu = (voltage - V_CLEAN) / (V_DIRTY - V_CLEAN) * MAX_NTU;

    // 限制范围，防止负数或超出量程
    if (ntu < 0.0f) ntu = 0.0f;
    if (ntu > 3000.0f) ntu = 3000.0f; 

    return ntu;
}