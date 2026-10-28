# -_zy

嵌入式与物联网项目代码

## 项目

| 目录 | 项目 | 技术栈 |
|---|---|---|
| [smart_cat_eye](./smart_cat_eye) | 智能猫眼（接近唤醒 + 抓拍 + 人脸识别报警） | 嵌入式 Linux、V4L2 / GStreamer、AP3216C、OpenCV、MQTT |
| [watch](./watch) | 多传感器融合与远程监控（心率/气压/姿态/GPS） | 嵌入式 Linux、I2C/IIO、卡尔曼滤波、Qt、MQTT |
| [smart_tank](./smart_tank) | 智能家用鱼缸（环境监测与远程控制） | STM32F103、OneNET / MQTT、多传感器、继电器、舵机、OLED |

各目录下均有独立 README，说明硬件接线、代码结构、编译与运行方法。