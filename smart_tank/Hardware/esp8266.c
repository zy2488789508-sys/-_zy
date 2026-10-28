#include "stm32f10x.h"
#include "esp8266.h"
#include "delay.h"
#include "usart.h"
#include "OLED.h"
#include <string.h>
#include <stdio.h>
#define ESP8266_WIFI_INFO		"AT+CWJAP=\"Ciallo\",\"666666zy\"\r\n"

unsigned char esp8266_buf[512];
unsigned short esp8266_cnt = 0, esp8266_cntPre = 0;

//==========================================================
//	函数名称：	ESP8266_Clear
//
//	函数功能：	清空缓存
//
//	入口参数：	无
//
//	返回参数：	无
//
//	说明：		
//==========================================================
void ESP8266_Clear(void)
{

	memset(esp8266_buf, 0, sizeof(esp8266_buf));
	esp8266_cnt = 0;

}

//==========================================================
//	函数名称：	ESP8266_WaitRecive
//
//	函数功能：	等待接收完成
//
//	入口参数：	无
//
//	返回参数：	REV_OK-接收完成		REV_WAIT-接收超时未完成
//
//	说明：		循环调用检测是否接收完成
//==========================================================
_Bool ESP8266_WaitRecive(void)
{

	if(esp8266_cnt == 0) 							//如果接收计数为0 则说明没有处于接收数据中，所以直接跳出，结束函数
		return REV_WAIT;
		
	if(esp8266_cnt == esp8266_cntPre)				//如果上一次的值和这次相同，则说明接收完毕
	{
		esp8266_cnt = 0;							//清0接收计数
			
		return REV_OK;								//返回接收完成标志
	}
		
	esp8266_cntPre = esp8266_cnt;					//置为相同
	
	return REV_WAIT;								//返回接收未完成标志

}

//==========================================================
//	函数名称：	ESP8266_SendCmd
//
//	函数功能：	发送命令
//
//	入口参数：	cmd：命令
//				res：需要检查的返回指令
//
//	返回参数：	0-成功	1-失败
//
//	说明：		
//==========================================================
_Bool ESP8266_SendCmd(char *cmd, char *res)
{
	
	unsigned char timeOut = 200;

	Usart_SendString(USART2, (unsigned char *)cmd, strlen((const char *)cmd));
	
	while(timeOut--)
	{
		if(ESP8266_WaitRecive() == REV_OK)							//如果收到数据
		{
			//UsartPrintf(USART_DEBUG, "RECV: %s\r\n", esp8266_buf);
			if(strstr((const char *)esp8266_buf, res) != NULL)		//如果检索到关键词
			{
				ESP8266_Clear();									//清空缓存
				
				return 0;
			}
		}
		
		DelayXms(10);
	}
	
	UsartPrintf(USART_DEBUG, "AT超时[%s]，模块回: %s\r\n", cmd, esp8266_buf); 	
	return 1;

}

//==========================================================
//	函数名称：	ESP8266_SendData
//
//	函数功能：	发送数据
//
//	入口参数：	data：数据
//				len：长度
//
//	返回参数：	无
//
//	说明：		
//==========================================================
_Bool ESP8266_SendCmdTimeOut(char *cmd, char *res, unsigned short timeOutMs);	//带超时的AT指令（实现在文件后面）

/* 连续发送失败计数：>=2 时主循环会主动重连（模块复位/掉线时能自动恢复） */
volatile uint16_t esp8266_send_fail_cnt = 0;

void ESP8266_SendData(unsigned char *data, unsigned short len)
{

	char cmdBuf[32];

	ESP8266_Clear();								//清空接收缓存
	sprintf(cmdBuf, "AT+CIPSEND=%d\r\n", len);		//发送命令
	if(ESP8266_SendCmd(cmdBuf, ">"))				//没等到'>'，模块忙
	{
		UsartPrintf(USART_DEBUG, "WARN: 没等到'>'，本包没发出去\r\n");
		esp8266_send_fail_cnt++;
		return;
	}
	Usart_SendString(USART2, data, len);			//发送数据

	/* 注意：这里不要等 SEND OK，也不要再 ESP8266_Clear()
	   否则会把同一块里紧跟着到达的 PUBACK 一起清掉，
	   导致"平台已接收数据"永远收不到 */

}

//==========================================================
//	函数名称：	ESP8266_GetIPD
//
//	函数功能：	获取平台返回的数据
//
//	入口参数：	等待的时间(乘以10ms)
//
//	返回参数：	平台返回的原始数据
//
//	说明：		不同网络设备返回的格式不同，需要去调试
//				如ESP8266的返回格式为	"+IPD,x:yyy"	x代表数据长度，yyy是数据内容
//==========================================================
unsigned char *ESP8266_GetIPD(unsigned short timeOut)
{

	char *ptrIPD = NULL;
	
	do
	{
		if(ESP8266_WaitRecive() == REV_OK)								//如果接收完成
		{
			ptrIPD = strstr((char *)esp8266_buf, "IPD,");				//搜索“IPD”头
			if(ptrIPD == NULL)											//如果没找到，可能是IPD头的延迟，还是需要等待一会，但不会超过设定的时间
			{
				//UsartPrintf(USART_DEBUG, "\"IPD\" not found\r\n");
			}
			else
			{
				ptrIPD = strchr(ptrIPD, ':');							//找到':'
				if(ptrIPD != NULL)
				{
					ptrIPD++;
					/* 打印收到的原始包（十六进制），诊断用 */
					UsartPrintf(USART_DEBUG, "RX:%02X %02X %02X %02X %02X\r\n",
																	(unsigned char)ptrIPD[0], (unsigned char)ptrIPD[1], (unsigned char)ptrIPD[2],
																	(unsigned char)ptrIPD[3], (unsigned char)ptrIPD[4]);
					return (unsigned char *)(ptrIPD);
				}
				else
					return NULL;
				
			}
		}
		
		DelayXms(5);													//延时等待
	} while(timeOut--);
	
	return NULL;														//超时还未找到，返回空指针

}

//==========================================================
//	函数名称：	ESP8266_Init
//
//	函数功能：	初始化ESP8266
//
//	入口参数：	无
//
//	返回参数：	无
//
//	说明：		
//==========================================================
//==========================================================
//	函数名称：	ESP8266_SendCmdTimeOut
//
//	函数功能：	发送AT指令并等待期望响应，可自定义超时时间
//
//	入口参数：	cmd：AT指令；res：期望的响应关键字；timeOutMs：超时时间(ms)
//
//	返回参数：	0-收到期望响应，1-超时或明确失败(FAIL)
//
//	说明：		CWJAP 这类慢指令必须给足时间，不能用默认 2 秒超时
//==========================================================
_Bool ESP8266_SendCmdTimeOut(char *cmd, char *res, unsigned short timeOutMs)
{
	Usart_SendString(USART2, (unsigned char *)cmd, strlen((const char *)cmd));

	while(timeOutMs)
	{
		if(ESP8266_WaitRecive() == REV_OK)
		{
			if(strstr((const char *)esp8266_buf, res) != NULL)
			{
				ESP8266_Clear();
				return 0;
			}

			if(strstr((const char *)esp8266_buf, "FAIL") != NULL)
			{
				UsartPrintf(USART_DEBUG, "ESP8266 返回: %s", esp8266_buf);
				ESP8266_Clear();
				return 1;
			}
		}

		DelayXms(10);
		timeOutMs -= 10;
	}

	return 1;
}


void ESP8266_Init(void)
{
	
//	GPIO_InitTypeDef GPIO_Initure;
//	
//	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

//	//ESP8266复位引脚
//	GPIO_Initure.GPIO_Mode = GPIO_Mode_Out_PP;
//	GPIO_Initure.GPIO_Pin = GPIO_Pin_14;					//GPIOC14-复位
//	GPIO_Initure.GPIO_Speed = GPIO_Speed_50MHz;
//	GPIO_Init(GPIOC, &GPIO_Initure);
//	
//	GPIO_WriteBit(GPIOC, GPIO_Pin_14, Bit_RESET);
//	DelayXms(250);
//	GPIO_WriteBit(GPIOC, GPIO_Pin_14, Bit_SET);
//	DelayXms(500);
	
	ESP8266_Clear();
	OLED_Clear();
	UsartPrintf(USART_DEBUG, "1. AT\r\n");
	OLED_ShowString(0,0,"1. AT...",OLED_6X8);
	OLED_Update();
	while(ESP8266_SendCmd("AT\r\n", "OK"))
	DelayXms(1000);
	
	UsartPrintf(USART_DEBUG, "2. CWMODE\r\n");
	OLED_ShowString(0,12,"2. CWMODE...",OLED_6X8);
	OLED_Update();
	while(ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK"))
		
		DelayXms(1000);
	
	UsartPrintf(USART_DEBUG, "3. AT+CWDHCP\r\n");
	OLED_ShowString(0,22,"3. AT+CWDHCP...",OLED_6X8);
	OLED_Update();
	while(ESP8266_SendCmd("AT+CWDHCP=1,1\r\n", "OK"))
		
		DelayXms(1000);
	
	UsartPrintf(USART_DEBUG, "4. CWJAP\r\n");
	OLED_ShowString(0,34,"4. CWJAP...",OLED_6X8);
	OLED_Update();
	ESP8266_SendCmd("AT+CWQAP\r\n", "OK");							//先断开可能残留的连接
	while(ESP8266_SendCmdTimeOut(ESP8266_WIFI_INFO, "GOT IP", 15000))	//单次给足15秒，别中途打断
	{
		UsartPrintf(USART_DEBUG, "4. CWJAP retry\r\n");
		ESP8266_SendCmd("AT+CWQAP\r\n", "OK");
		DelayXms(1000);
	}
	UsartPrintf(USART_DEBUG, "5. ESP8266 Init OK\r\n");
	OLED_ShowString(0,46,"5. ESP8266 Init OK",OLED_6X8);
	OLED_Update();
	DelayXms(1000);
	OLED_Clear();
	//OLED_ShowImage(112, 0, 16, 16,WIFI);
	//OLED_Update();

}

//==========================================================
//	函数名称：	USART2_IRQHandler
//
//	函数功能：	串口2收发中断
//
//	入口参数：	无
//
//	返回参数：	无
//
//	说明：		
//==========================================================
void USART2_IRQHandler(void)
{

	if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) //接收中断
	{
		if(esp8266_cnt >= sizeof(esp8266_buf))	esp8266_cnt = 0; //防止串口被刷爆
		esp8266_buf[esp8266_cnt++] = USART2->DR;
		
		USART_ClearFlag(USART2, USART_FLAG_RXNE);
	}

}
