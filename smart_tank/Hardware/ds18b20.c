#include "stm32f10x.h"
#include "ds18b20.h"
#include "Delay.h"
#define DS_PORT GPIOB
#define DS_PIN  GPIO_Pin_1
// 切换为输出模式
static void DS_Mode_Out(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = DS_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS_PORT, &GPIO_InitStructure);
}

// 切换为上拉输入模式
static void DS_Mode_In(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = DS_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS_PORT, &GPIO_InitStructure);
}

// 复位 DS18B20，返回 0 表示成功，1 表示失败
static uint8_t DS_Reset(void) {
    uint8_t presence = 0;
    DS_Mode_Out();
    GPIO_ResetBits(DS_PORT, DS_PIN);
    DelayUs(480); // 拉低至少 480us
    GPIO_SetBits(DS_PORT, DS_PIN);
    DS_Mode_In();
    DelayUs(60); // 等待 15~60us 后采样应答
    presence = GPIO_ReadInputDataBit(DS_PORT, DS_PIN);
    DelayUs(420); // 等待应答脉冲结束
    return presence; // 0 为成功
}

// 写一个字节
static void DS_WriteByte(uint8_t data) {
    DS_Mode_Out();
    for (int i = 0; i < 8; i++) {
        GPIO_ResetBits(DS_PORT, DS_PIN);
        DelayUs(2);
        if (data & 0x01) {
            GPIO_SetBits(DS_PORT, DS_PIN);
        }
        DelayUs(60);
        GPIO_SetBits(DS_PORT, DS_PIN);
        DelayUs(2);
        data >>= 1;
    }
}

// 读一个字节
static uint8_t DS_ReadByte(void) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data >>= 1;
        DS_Mode_Out();
        GPIO_ResetBits(DS_PORT, DS_PIN);
        DelayUs(2);
        GPIO_SetBits(DS_PORT, DS_PIN);
        DS_Mode_In();
        DelayUs(12); // 在 15us 内采样
        if (GPIO_ReadInputDataBit(DS_PORT, DS_PIN)) {
            data |= 0x80;
        }
        DelayUs(50);
    }
    return data;
}

// 初始化函数
void ds18b20_init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    DS_Mode_Out();
    GPIO_SetBits(DS_PORT, DS_PIN); // 空闲时拉高
}

// 读取温度值（摄氏度），失败返回 -100.0
float ds18b20_get_temp(void) {
    uint8_t temp_l, temp_h;
    int16_t temp_raw;

    if (DS_Reset() != 0) 
		return -100.0f; // 复位失败

    DS_WriteByte(0xCC); // 跳过 ROM
    DS_WriteByte(0x44); // 启动温度转换

    // 等待转换完成（12位精度约需750ms）
    DelayXms(325); 

    if (DS_Reset() != 0) 
		return -100.0f;

    DS_WriteByte(0xCC); // 跳过 ROM
    DS_WriteByte(0xBE); // 读取暂存器

    temp_l = DS_ReadByte(); // 低 8 位
    temp_h = DS_ReadByte(); // 高 8 位

    temp_raw = (temp_h << 8) | temp_l;
    
    // 12位精度下，分辨率为 0.0625℃
    return temp_raw * 0.0625f;
}