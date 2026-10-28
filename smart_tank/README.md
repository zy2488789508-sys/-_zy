# 智能家用鱼缸（STM32F103 + OneNET 云平台）

基于 STM32F103C8T6 的鱼缸环境监测与远程控制系统：本地采集 5 路传感器并在 OLED 上实时显示，
经 ESP8266 通过 MQTT 接入中国移动 OneNET 云平台，实现数据的云端上报与设备的远程控制。

## 功能

- **数据采集**：水温（DS18B20）、浊度（TS300）、TDS、PH、超声波水位（HC-SR04），2 s 周期采集
- **本地显示**：0.96 寸 OLED（I2C）实时显示浊度 / TDS / 水温 / PH 与照明灯状态
- **云端上报**：MQTT + OneNET 物模型（OneJSON），9 个属性，QoS1
- **远程控制**：照明灯、抽水泵、放水泵、增氧泵（3 路继电器）、舵机角度，支持云端下发与状态回传
- **本地按键**：5 个按键分别控制照明灯、3 路水泵、舵机（外部中断 EXTI）

## 硬件清单

| 模块 | 型号 / 接口 | 引脚 |
|---|---|---|
| 主控 | STM32F103C8T6 | — |
| WiFi | ESP8266 | USART2 |
| 水温 | DS18B20 | PB1 |
| 浊度 | TS300（ADC） | — |
| TDS | TDS 电极（ADC） | — |
| PH | PH 电极（ADC） | — |
| 水位 | HC-SR04 | TRIG PB10 / ECHO PB11 + TIM2 |
| 显示 | 0.96" OLED I2C | — |
| 照明灯 | GPIO 输出 | PC13 |
| 继电器 1/2/3 | PA5 / PA6 / PA7 | 放水泵 / 抽水泵 / 增氧泵 |
| 舵机 | TIM4_CH1 PWM | PB6 |
| 按键 | PB7 / PB12 / PB13 / PB14 / PB15 | EXTI 中断 |

## 物模型（9 个属性）

| 标识符 | 名称 | 类型 | 读写 |
|---|---|---|---|
| `zhuodu` | 浊度 | float | 只读 |
| `tds_value` | 水质TDS | float | 只读 |
| `water_temp` | 水温 | float | 只读 |
| `PH` | PH值 | float | 只读 |
| `led` | 照明灯 | bool | 读写 |
| `drain_pump` | 抽水泵 | bool | 读写 |
| `fill_pump` | 放水泵 | bool | 读写 |
| `oxygen` | 增氧泵 | bool | 读写 |
| `servo` | 舵机角度 | int32(0~90) | 读写 |

完整的物模型定义见 `物模型_智能鱼缸.json`，可直接在 OneNET 控制台「导入物模型」。

## 目录结构

```
smart_tank/
├─ Start/       启动文件、CMSIS、系统时钟
├─ Library/     STM32F10x 标准外设库
├─ Hardware/    板级驱动：Key、LED、light、Relay、Servo、PWM、
│               OLED、ds18b20、ts300、tds、ph、hc04、beep、usart、esp8266
├─ System/      延时函数（SysTick）
├─ User/        main.c、中断服务
├─ NET/         MQTT（MqttKit）、OneNET 接入（onenet）、cJSON
├─ project.uvprojx   Keil MDK 工程
└─ 物模型_智能鱼缸.json
```

## 编译与烧录

- IDE：Keil MDK 5（ARMCC 5.06），工程已开启 **C99**
- 直接打开 `project.uvprojx` → Rebuild → 下载
- 串口 1（115200）输出调试日志，串口 2（115200）驱动 ESP8266

## 使用前需修改

运行前请把下面两处改成自己的配置：

1. `Hardware/esp8266.c` 第 8 行：WiFi 名称与密码
   `#define ESP8266_WIFI_INFO  "AT+CWJAP=\"你的WiFi\",\"你的密码\"\r\n"`
2. `NET/onenet/src/onenet.c` 第 49~53 行：OneNET 产品 ID / 设备密钥 / 设备名称

## 实现要点

- 中断服务与主循环解耦：按键走 EXTI 外部中断，采样与上报走主循环定时
- 上报使用 QoS1 + 递增消息 ID，避免平台把重复 ID 的报文当重复消息丢弃
- 心跳与 PUBACK 双重判据检测链路，连续发送失败立即重连，掉线可自恢复
- ADC 采样与超声回波均带超时保护，单个传感器异常不会阻塞主循环