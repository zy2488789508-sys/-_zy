#include "ph.h"
#include "stm32f10x_adc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

// 动态校准参数 (默认值先随便给，main里校准后会覆盖)
float ph_slope = -6.7f;   
float ph_offset = 18.204f; 

// 去极值平均滤波
static double avergearray(int* arr, int number) {
    int i;
    int max, min;
    double avg;
    long amount = 0;
    if (number <= 0) return 0;
    if (number < 5) {
        for(i = 0; i < number; i++) amount += arr[i];
        return (double)amount / number;
    } else {
        if(arr[0] < arr[1]) { min = arr[0]; max = arr[1]; }
        else { min = arr[1]; max = arr[0]; }
        for(i = 2; i < number; i++) {
            if(arr[i] < min) { amount += min; min = arr[i]; }
            else { 
                if(arr[i] > max) { amount += max; max = arr[i]; }
                else { amount += arr[i]; } 
            }
        }
        avg = (double)amount / (number - 2);
    }
    return avg;
}

void PH_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;

    // 使用 GPIOB + ADC1
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_ADC1, ENABLE);

    // PB0 作为 PH 模拟输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    // PB0 对应 ADC1_IN8
    ADC_RegularChannelConfig(ADC1, ADC_Channel_8, 1, ADC_SampleTime_239Cycles5);
    ADC_Cmd(ADC1, ENABLE);

    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

float PH_ReadValue(void)
{
    int pHArray[40]; 
    double avg_adc_value = 0.0;
    float voltage_value = 0.0;
    float ph_value = 0.0;
    int i;
    volatile int j;

    for(i = 0; i < 40; i++)
    {
        { uint16_t g = 0; while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) { if(++g > 20000) break; } }
        pHArray[i] = ADC_GetConversionValue(ADC1);
        for(j = 0; j < 5000; j++);
    }

    avg_adc_value = avergearray(pHArray, 40);
    voltage_value = (float)avg_adc_value / 4096.0 * 3.3;

    ph_value = ph_slope * voltage_value + ph_offset;

    if(ph_value <= 0.0) ph_value = 0.0;
    if(ph_value > 14.0) ph_value = 14.0;

    return ph_value;
}

// 【修正后的两点校准公式】
void PH_Calibrate(float v1, float ph1, float v2, float ph2)
{
    // 1. 算出真实斜率 (pH变化量 / 电压变化量)
    ph_slope = (ph2 - ph1) / (v2 - v1);
    
    // 2. 反推截距
    ph_offset = ph1 - ph_slope * v1;
}