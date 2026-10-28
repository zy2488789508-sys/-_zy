#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "light.h"
#include "Key.h"
#include "Servo.h"
#include "hc04.h"
#include "ds18b20.h"
#include "ts300.h"
#include "tds.h"
#include "ph.h"
#include "Relay.h"
#include "beep.h"
#include <stdio.h>
#include "usart.h"
#include "esp8266.h"
#include "onenet.h"
#include "MqttKit.h"

#define ESP8266_ONENET_INFO		"AT+CIPSTART=\"TCP\",\"mqtts.heclouds.com\",1883\r\n"

/* 采集/上传/显示共用的全局变量（onenet.c 里 extern 引用了 turbidity、tds_value、temperature、ph_value） */
float temperature = 0;		//水温 ℃
float turbidity   = 0;		//浊度 NTU
float tds_value   = 0;		//水质 ppm
float ph_value    = 0;		//PH
float distance    = 0;		//水位/测距 cm

void Refresh_Data(void);
void Read_Sensor(void);
void OneNET_Connect(void);
uint8_t MQTT_Ping(void);
extern volatile uint16_t mqtt_ack_count;		//平台确认计数（在 onenet.c 里累加）
extern volatile uint16_t esp8266_send_fail_cnt;	//ESP8266 连续发送失败计数（在 esp8266.c 里）

/*
************************************************************
*	函数名称：	Hardware_Init
*
*	函数功能：	硬件初始化
*
*	说明：		初始化单片机功能以及外接设备（不含ESP8266）
************************************************************
*/
void Hardware_Init(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);	//中断控制器分组设置
	Delay_Init();									//systick初始化
	Usart1_Init(115200);							//串口1，打印信息用
	Usart2_Init(115200);							//串口2，驱动ESP8266用
	OLED_Init();									//OLED屏幕初始化
	KEY_EXTI_Init();								//按键外部中断：PB7控灯，PB12~PB15控继电器/舵机
	Light_init();									//照明灯
	Servo_Init();									//舵机
	hc04_init();									//HC-SR04 测距
	time_init();									//TIM2 1us 计时，测距用
	ds18b20_init();									//水温
	ts300b_init();									//浊度
	TDS_Init();										//TDS
	PH_Init();										//PH
	Relay_init();									//继电器
	beep_init();									//蜂鸣器
	PH_Calibrate(1.72f, 6.68f, 2.12f, 4.00f);		//PH两点校准

	UsartPrintf(USART_DEBUG, " Hardware init OK\r\n");
	OLED_Clear();
	OLED_ShowString(0,0,"Hardware init OK",OLED_8X16);
	OLED_Update();
	DelayMs(1000);
	OLED_Clear();
	OLED_Update();
}

/*
************************************************************
*	函数名称：	OneNET_Connect
*
*	函数功能：	连接WiFi和OneNET平台（初始化时、断线重连时都调用它）
*
*	说明：		各步骤失败会一直重试，所以调用本函数返回时一定是连上的
************************************************************
*/
void OneNET_Connect(void)
{
	ESP8266_Init();									//AT / CWMODE / CWDHCP / CWJAP，内部失败会重试
	ESP8266_SendCmd("AT+CIPCLOSE\r\n", "OK");		//先关掉可能残留的TCP连接

	UsartPrintf(USART_DEBUG, "Connect MQTTs Server...\r\n");
	OLED_ShowString(0,0,"Connect MQTTs Server...",OLED_6X8);
	OLED_Update();
	while(ESP8266_SendCmd(ESP8266_ONENET_INFO, "CONNECT"))
	DelayXms(500);

	UsartPrintf(USART_DEBUG, "Connect MQTT Server Success\r\n");
	OLED_ShowString(0,22,"Connect MQTT Server Success...",OLED_6X8);
	OLED_Update();
	{
		uint8_t login_fail = 0;
		
		while(OneNet_DevLink())						//接入OneNET
		{
			UsartPrintf(USART_DEBUG, "MQTT 登录未成功，准备重试\r\n");
			if(++login_fail >= 2)					//同一条TCP连两次还不成功，就关掉重开
			{
				login_fail = 0;
				UsartPrintf(USART_DEBUG, "重开TCP连接...\r\n");
				ESP8266_SendCmd("AT+CIPCLOSE\r\n", "OK");
				DelayXms(300);
				while(ESP8266_SendCmd(ESP8266_ONENET_INFO, "CONNECT"))
				DelayXms(500);
			}
			DelayXms(500);
		}
	}
	OLED_Clear();
	OLED_Update();
	
	OneNET_Subscribe();								//订阅平台下发属性的主题
}

/*
************************************************************
*	函数名称：	MQTT_Ping
*
*	函数功能：	发一次MQTT心跳，判断链路是否还活着
*
*	返回参数：	1=在线，0=掉线（需要重连）
************************************************************
*/
uint8_t MQTT_Ping(void)
{
	MQTT_PACKET_STRUCTURE mqttPacket = {NULL, 0, 0, 0};
	unsigned char *dataPtr;

	if(MQTT_PacketPing(&mqttPacket) != 0)
		return 0;

	ESP8266_SendData(mqttPacket._data, mqttPacket._len);
	MQTT_DeleteBuffer(&mqttPacket);

	dataPtr = ESP8266_GetIPD(100);					//约等500ms看有没有回应
	if(dataPtr == NULL)
		dataPtr = ESP8266_GetIPD(100);				//再等一轮，避免偶尔丢包被误判掉线

	if(dataPtr != NULL)
	{
		OneNet_RevPro(dataPtr);						//是心跳回应就走默认分支，是下行报文就顺手处理掉
		return 1;
	}

	UsartPrintf(USART_DEBUG, "心跳无回应\r\n");
	return 0;
}

/*
************************************************************
*	函数名称：	main
*
*	函数功能：	主函数：初始化 -> 连OneNET -> 循环采集、显示、上报
************************************************************
*/
int main(void)
{
	unsigned short timeCount = 0;			//发送间隔变量
	unsigned short pingCount = 0;			//心跳检查计数
	unsigned char *dataPtr = NULL;
	uint16_t lastAckCount = 0;				//上一次检查时的 PUBACK 计数

	Hardware_Init();						//初始化外围硬件

	/* 打印本次复位原因，用来区分"真复位"和"重连" */
	UsartPrintf(USART_DEBUG, "\r\n===== 芯片启动(本次为真复位) =====\r\n");
	if(RCC_GetFlagStatus(RCC_FLAG_PORRST) != RESET)  UsartPrintf(USART_DEBUG, "  原因: 上电复位\r\n");
	if(RCC_GetFlagStatus(RCC_FLAG_PINRST) != RESET)  UsartPrintf(USART_DEBUG, "  原因: 复位脚复位\r\n");
	if(RCC_GetFlagStatus(RCC_FLAG_SFTRST) != RESET)  UsartPrintf(USART_DEBUG, "  原因: 软件复位\r\n");
	if(RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) UsartPrintf(USART_DEBUG, "  原因: 独立看门狗复位(程序跑飞)\r\n");
	if(RCC_GetFlagStatus(RCC_FLAG_WWDGRST) != RESET) UsartPrintf(USART_DEBUG, "  原因: 窗口看门狗复位(程序跑飞)\r\n");
	if(RCC_GetFlagStatus(RCC_FLAG_LPWRRST) != RESET) UsartPrintf(USART_DEBUG, "  原因: 低功耗复位\r\n");
	RCC_ClearFlag();

	OneNET_Connect();						//连WiFi + OneNET
	Refresh_Data();							//先刷一次屏，确认显示链路正常

	while(1)
	{
		/* 发送连续失败：模块复位或TCP已断，立刻重连（不用等轮询周期） */
		if(esp8266_send_fail_cnt >= 2)
		{
			esp8266_send_fail_cnt = 0;
			UsartPrintf(USART_DEBUG, "发送失败，立即重连...\r\n");
			OneNET_Connect();
			timeCount = 0;
			lastAckCount = mqtt_ack_count;
			continue;
		}

		if(++timeCount >= 200)				//10ms * 200 = 2s，每2秒采集并上报一次
		{
			Refresh_Data();					//先用上一次的数据刷屏，保证屏幕一定在动
			UsartPrintf(USART_DEBUG, "采集传感器...\r\n");
			Read_Sensor();					//读取全部传感器
			UsartPrintf(USART_DEBUG, "T=%.1f TU=%.0f TDS=%.0f PH=%.1f\r\n",
						temperature, turbidity, tds_value, ph_value);
			Refresh_Data();					//用新数据再刷一次
			UsartPrintf(USART_DEBUG, "OneNet_SendData\r\n");
			OneNet_SendData();				//上报到OneNET
			timeCount = 0;
			ESP8266_Clear();
		}

		if(++pingCount >= 1500)				//10ms * 1500 = 15s，检查一次链路
		{
			pingCount = 0;
			if(esp8266_send_fail_cnt >= 2 || mqtt_ack_count == lastAckCount)	//发不出去 或 长时间收不到平台确认
			{
				esp8266_send_fail_cnt = 0;
				UsartPrintf(USART_DEBUG, "MQTT offline, reconnecting...\r\n");
				OLED_Clear();
				OLED_ShowString(0,24,"Net Lost, Reconnect...",OLED_8X16);
				OLED_Update();
				OneNET_Connect();			//阻塞式重连，连上后继续
				timeCount = 0;
				lastAckCount = mqtt_ack_count;
			}
			else
				lastAckCount = mqtt_ack_count;
		}

		dataPtr = ESP8266_GetIPD(0);
		if(dataPtr != NULL)
			OneNet_RevPro(dataPtr);			//处理平台下发的命令

		DelayXms(10);
	}
}

/*
************************************************************
*	函数名称：	Read_Sensor
*
*	函数功能：	读取全部传感器
*
*	说明：		TDS 用当前水温做温度补偿，所以先读水温
************************************************************
*/
void Read_Sensor(void)
{
	UsartPrintf(USART_DEBUG, "  DS18B20:");
	temperature = ds18b20_get_temp();				//水温 ℃
	UsartPrintf(USART_DEBUG, " %.1f\r\n", temperature);

	UsartPrintf(USART_DEBUG, "  TDS    :");
	tds_value   = TDS_ReadValue(temperature);		//水质 ppm
	UsartPrintf(USART_DEBUG, " %.0f\r\n", tds_value);

	UsartPrintf(USART_DEBUG, "  TS300  :");
	turbidity   = ts300b_get_turbidity();			//浊度 NTU
	UsartPrintf(USART_DEBUG, " %.0f\r\n", turbidity);

	UsartPrintf(USART_DEBUG, "  PH     :");
	ph_value    = PH_ReadValue();					//PH
	UsartPrintf(USART_DEBUG, " %.1f\r\n", ph_value);

	UsartPrintf(USART_DEBUG, "  HC04   :");
	distance    = hc04_get_distance();				//水位/测距 cm
	UsartPrintf(USART_DEBUG, " %.1f\r\n", distance);
}

/*
************************************************************
*	函数名称：	Refresh_Data
*
*	函数功能：	OLED 刷新显示
************************************************************
*/
void Refresh_Data(void)
{
	OLED_Clear();
	OLED_ShowImage(112, 0, 16, 16,WIFI);
	OLED_Printf(0, 0, OLED_8X16,"浊度:%.0f NTU",turbidity);
	OLED_Printf(0, 16, OLED_8X16,"水质:%.0f PPM",tds_value);
	OLED_Printf(0, 32, OLED_8X16,"水温:%.1f ℃", temperature);
	OLED_Printf(0, 48, OLED_8X16,"PH:%.1f 灯:%s", ph_value, Light_GetState() ? "开" : "关");
	OLED_Update();
}
