#include "tds.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

// 中值滤波：去除最大最小值后求平均
static double medianFilter(int* arr, int number) {
    int i, j, temp;
    long amount = 0;
    for(i = 0; i < number - 1; i++)
        for(j = 0; j < number - 1 - i; j++)
            if(arr[j] > arr[j + 1]) {
                temp = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = temp;
            }
    for(i = 1; i < number - 1; i++) amount += arr[i];
    return (double)amount / (number - 2);
}

void TDS_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);

    // 使用 PA0 作为 TDS 模拟输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    // 对应通道0（PA0）
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_239Cycles5);
    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

// 读取并计算电压
float TDS_ReadVoltage(void)
{
    int tdsArray[30];
    double avg_adc_value = 0.0;
    int i;
    volatile int j;

    for(i = 0; i < 30; i++)
    {
        { uint16_t g = 0; while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) { if(++g > 20000) break; } }
        tdsArray[i] = ADC_GetConversionValue(ADC1);
        for(j = 0; j < 5000; j++);
    }

    avg_adc_value = medianFilter(tdsArray, 30);
    return (float)avg_adc_value / 4096.0 * 3.3;
}

// 计算TDS值 (传入电压和温度)
float TDS_ReadValue(float temperature)
{
    float voltage = TDS_ReadVoltage();
    float compensationCoefficient = 1.0f;
    float compensationVoltage = 0.0f;
    float tdsValue = 0.0f;

    // 温度补偿
    compensationCoefficient = 1.0 + 0.02 * (temperature - 25.0);
    compensationVoltage = voltage / compensationCoefficient;

    // 三次多项式拟合
    tdsValue = (133.42 * compensationVoltage * compensationVoltage * compensationVoltage
              - 255.86 * compensationVoltage * compensationVoltage
              + 857.39 * compensationVoltage) * 0.5;

    if(tdsValue < 0) tdsValue = 0;

    return tdsValue;
}