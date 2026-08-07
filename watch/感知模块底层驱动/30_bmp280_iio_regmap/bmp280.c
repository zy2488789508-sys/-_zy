#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/unaligned/be_byteshift.h>
#include <linux/iio/trigger.h>
#include "bmp280.h"
#define BMP280_NUM			1
#define BMP280_NAME			"bmp280"
struct bmp280_device {
	struct i2c_client *client;
	dev_t devid;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	struct device_node *nd;
	struct regmap *regmap;
	struct regmap_config regmap_config;
	struct mutex lock;
	unsigned int Temperature;
	unsigned int pressure;
	u16 dig_T1;
    s16 dig_T2;
    s16 dig_T3;
    u16 dig_P1;
    s16 dig_P2;
    s16 dig_P3;
    s16 dig_P4;
    s16 dig_P5;
    s16 dig_P6;
    s16 dig_P7;
    s16 dig_P8;
    s16 dig_P9;

    s32 t_fine; // 中间值
};
/* 温度补偿函数（传入原始 ADC 值，返回实际温度 * 100，单位 0.01℃） */
static s32 bmp280_compensate_temp(struct bmp280_device *dev, s32 adc_T)
{
    s32 var1, var2;
    var1 = ((((adc_T >> 3) - ((s32)dev->dig_T1 << 1))) * ((s32)dev->dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((s32)dev->dig_T1)) * ((adc_T >> 4) - ((s32)dev->dig_T1))) >> 12) * ((s32)dev->dig_T3)) >> 14;
    dev->t_fine = var1 + var2;
    return (dev->t_fine * 5 + 128) >> 8;  // 返回 25.21℃ 就是 2521
}

/* 压力补偿函数（传入原始 ADC 值，返回实际压力，单位 Pa） */
static u32 bmp280_compensate_press(struct bmp280_device *dev, s32 adc_P)
{
    s32 var1, var2;
    u32 p;
    var1 = (((s32)dev->t_fine) >> 1) - (s32)64000;
    var2 = (((var1 >> 2) * (var1 >> 2)) >> 11) * ((s32)dev->dig_P6);
    var2 = var2 + ((var1 * ((s32)dev->dig_P5)) << 1);
    var2 = (var2 >> 2) + (((s32)dev->dig_P4) << 16);
    var1 = (((dev->dig_P3 * (((var1 >> 2) * (var1 >> 2)) >> 13)) >> 3) + ((((s32)dev->dig_P2) * var1) >> 1)) >> 18;
    var1 = ((((32768 + var1)) * ((s32)dev->dig_P1)) >> 15);
    if (var1 == 0) return 0;
    p = (((u32)(((s32)1048576 - adc_P) - (var2 >> 12))) * 3125);
    if (p < 0x80000000)
        p = (p << 1) / ((u32)var1);
    else
        p = (p / (u32)var1) * 2;
    var1 = (((s32)dev->dig_P9) * ((s32)(((p >> 3) * (p >> 3)) >> 13))) >> 12;
    var2 = (((s32)(p >> 2)) * ((s32)dev->dig_P8)) >> 13;
    p = (u32)((s32)p + ((var1 + var2 + dev->dig_P7) >> 4));
    return p;
}
/*往bmp280寄存器写*/
static void bmp280_write_reg(struct bmp280_device *dev, u8 reg, u8 data)
{
	regmap_write(dev->regmap, reg, data);
}
/*从bmp280寄存器read*/
static char bmp280_read_reg(struct bmp280_device *dev, u8 reg)
{
	u8 ret = 0;
	unsigned int data; 
	ret = regmap_read(dev->regmap,reg,&data);
	return (u8)data;
}
enum inv_bmp280_scan{
	BMP280_SCAN_TEMP,
    BMP280_SCAN_PRESS,
};
/*设备通道描述结构体*/
static const struct iio_chan_spec bmp280_channels[] = 
{
  /* 温度通道 */
    {
        .type = IIO_TEMP,
        .address = BMP280_REG_TEMP_MSB,   /* 温度数据起始地址 0xFA */
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index = BMP280_SCAN_TEMP,
        .scan_type = {
            .sign = 'u',          /* 无符号 */
            .realbits = 20,       /* 有效位数 20 位 */
            .storagebits = 32,    /* 存储用 32 位 */
            .endianness = IIO_LE, /* 小端 */
        },
    },
    /* 压力通道 */
    {
        .type = IIO_PRESSURE,
        .address = BMP280_REG_PRESS_MSB,  /* 压力数据起始地址 0xF7 */
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .scan_index = BMP280_SCAN_PRESS,
        .scan_type = {
            .sign = 'u',          /* 无符号 */
            .realbits = 20,       /* 有效位数 20 位 */
            .storagebits = 32,    /* 存储用 32 位 */
            .endianness = IIO_LE, /* 小端 */
        },
    },
};
	/*
 * 扫描掩码，两种情况，全启动0X111，或者都不启动0X0
 */
static const unsigned long bmp280_scan_masks[] = {
	BIT(BMP280_SCAN_TEMP) | BIT(BMP280_SCAN_PRESS), 0,
};

static int bmp280_read_raw(struct iio_dev *indio_dev,
                           struct iio_chan_spec const *chan,
                           int *val, int *val2, long mask)
{
    struct bmp280_device *bmp280dev = iio_priv(indio_dev);
    u8 data[6];
    s32 adc_T, adc_P;
    s32 temp_out;
    u32 press_out;
    int ret;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        mutex_lock(&bmp280dev->lock);

        /* 1. 突发读取 6 字节 */
        ret = regmap_bulk_read(bmp280dev->regmap, BMP280_REG_PRESS_MSB, data, 6);
        if (ret < 0) {
            mutex_unlock(&bmp280dev->lock);
            return ret;
        }

        /* 2. 拼接原始 20 位数据 */
        adc_P = (s32)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
        adc_T = (s32)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));

        /* 3. 根据通道类型进行补偿计算 */
        switch (chan->type) {
        case IIO_TEMP:
            temp_out = bmp280_compensate_temp(bmp280dev, adc_T);
            *val = (int)temp_out;   // 单位 0.01℃
            break;
        case IIO_PRESSURE:
            press_out = bmp280_compensate_press(bmp280dev, adc_P);
            *val = (int)press_out;  // 单位 Pa
            break;
        default:
            mutex_unlock(&bmp280dev->lock);
            return -EINVAL;
        }

        mutex_unlock(&bmp280dev->lock);
        return IIO_VAL_INT;   // 直接返回整数，无需 scale

    case IIO_CHAN_INFO_SCALE:
        /* 因为 raw 已经补偿，scale 应为 1（或 100 等）/
        *val = 1;
        return IIO_VAL_INT;

    default:
        return -EINVAL;
    }
}
static int bmp280_write_raw(struct iio_dev *indio_dev, 
                  struct iio_chan_spec const *chan, 
                  int val, int val2, long mask) 
{
	return -EINVAL;
}
  static int bmp280_write_raw_get_fmt(struct iio_dev *indio_dev, 
                   struct iio_chan_spec const *chan, long mask) 
{
	return -EINVAL;
}
static const struct iio_info bmp280_info = { 
    .read_raw       = bmp280_read_raw, 
    .write_raw      = bmp280_write_raw, 
    .write_raw_get_fmt = &bmp280_write_raw_get_fmt, 
	 
};    
/*芯片初始化
*设备结构体 dev
*寄存器地址	reg
*寄存器写入的值 data
*/
static void bmp280_dev_init(struct bmp280_device *dev, u8 reg, u8 data)
{
	bmp280_write_reg(dev,reg,data);
	usleep_range(2000,3000);/*延时2-3ms*/
	bmp280_write_reg(dev,BMP280_REG_CTRL_MEAS,0x2F);//选择模式：正常模式
	usleep_range(2000,3000);/*延时2-3ms*/
	bmp280_write_reg(dev,BMP280_REG_CONFIG,0x1C);//选择休眠时间，以及滤波格式
}
static void bmp280_data_Calibrate(struct bmp280_device *dev)
{
	u8 ret = 0;
	u8 buf[24] = {0};
	ret = regmap_bulk_read(dev->regmap,BMP280_REG_DIG_T1_LSB,buf,24);
	dev->dig_T1 = ((buf[1]) << 8 | buf[0]);
	dev->dig_T2 = ((buf[3]) << 8 | buf[2]);
	dev->dig_T3 = ((buf[5]) << 8 | buf[4]);
	dev->dig_P1 = ((buf[7]) << 8 | buf[6]);
	dev->dig_P2 = ((buf[9]) << 8 | buf[8]);
	dev->dig_P3 = ((buf[11]) << 8 | buf[10]);
	dev->dig_P4 = ((buf[13]) << 8 | buf[12]);
	dev->dig_P5 = ((buf[15]) << 8 | buf[14]);
	dev->dig_P6 = ((buf[17]) << 8 | buf[16]);
	dev->dig_P7= ((buf[19]) << 8 | buf[18]);
	dev->dig_P8 = ((buf[21]) << 8 | buf[20]);
	dev->dig_P9 = ((buf[23]) << 8 | buf[22]);
}

static int bmp280_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	unsigned char data = 0;
	struct bmp280_device *bmp280dev;
	struct iio_dev *indio_dev;
	/*申请iio_dev内存*/
	indio_dev = devm_iio_device_alloc(&client->dev,sizeof(*bmp280dev));
	if(!indio_dev)
	{
		return -ENOMEM;
	}
	/*获取私有结构体地址*/
	bmp280dev = iio_priv(indio_dev);
	bmp280dev->client = client;//保存内核给的数据给私有结构体
	i2c_set_clientdata(client,indio_dev);//让内核可以访问client
	/* 初始化regmap_config设置 */
	bmp280dev->regmap_config.reg_bits = 8;
	bmp280dev->regmap_config.val_bits = 8;
	/*初始化作为i2c1接口的regmap*/
	bmp280dev->regmap = regmap_init_i2c(client,&bmp280dev->regmap_config);
	if (IS_ERR(bmp280dev->regmap)) {
		ret = PTR_ERR(bmp280dev->regmap);
		goto err_regmap_init;
	}	
	/*初始化互斥锁*/
	mutex_init(&bmp280dev->lock);
	/*iio_dev其他成员变量初始化*/
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &bmp280_info;
	indio_dev->name = BMP280_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = bmp280_channels;
	indio_dev->num_channels = ARRAY_SIZE(bmp280_channels);
	indio_dev->available_scan_masks = bmp280_scan_masks;
	/* 5、注册iio_dev */
	ret = iio_device_register(indio_dev);
	if(ret < 0)
	{
		dev_err(&client->dev, "iio_device_register failed\n");
		goto err_iio_register;
	}
	printk("probe init successful\r\n");
	bmp280_dev_init(bmp280dev,BMP280_REG_RESET,0xB6);//bmp280芯片复位
	data = bmp280_read_reg(bmp280dev,BMP280_REG_ID);
	printk("0XD0寄存器的值为%#x\r\n",data);
	bmp280_data_Calibrate(bmp280dev);//读取校准值
	return 0;
err_iio_register:
	regmap_exit(bmp280dev->regmap);
err_regmap_init:
	return ret;
}
static int bmp280_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bmp280_device *bmp280dev;
	bmp280dev = iio_priv(indio_dev);
	/* 5、释放regmap */
	regmap_exit(bmp280dev->regmap);
	/*注销IIO*/
	iio_device_unregister(indio_dev);
	printk("remove successful\r\n");
	return 0;
}

static const struct i2c_device_id bmp280_id[] = {
	{"bosch,bmp280", 0},
	{}
};
static const struct of_device_id bmp280_of_match[] = {
	{ .compatible = "bosch,bmp280" },
	{ /* Sentinel */ }
};
/*i2c1下的驱动信息*/
static struct i2c_driver bmp280_driver = {
	.probe = bmp280_probe,
	.remove = bmp280_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "bmp280",
		.of_match_table = bmp280_of_match,
	},
	.id_table = bmp280_id,
};


static int __init bmp280_init(void)
{
	int ret = 0;
	ret = i2c_add_driver(&bmp280_driver);
	return ret;
}
static void __exit bmp280_exit(void)
{
	i2c_del_driver(&bmp280_driver);
}

module_init(bmp280_init);
module_exit(bmp280_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("yu");