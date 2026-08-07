#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>

/* ============ 寄存器定义 ============ */
#define REG_FIFO_WR_PTR    0x04
#define REG_OVF_COUNTER    0x05
#define REG_FIFO_RD_PTR    0x06
#define REG_FIFO_DATA      0x07
#define REG_FIFO_CONFIG    0x08
#define REG_MODE_CONFIG    0x09
#define REG_SPO2_CONFIG    0x0A
#define REG_LED1_PA        0x0C
#define REG_LED2_PA        0x0D
#define REG_TEMP_INT       0x1F
#define REG_TEMP_FRAC      0x20
#define REG_TEMP_CONFIG    0x21
#define REG_PART_ID        0xFF

#define SAMPLE_PERIOD      msecs_to_jiffies(10)
#define KFIFO_SIZE         256

/* ============ 数据结构 ============ */
struct sample_data {
    uint32_t red;
    uint32_t ir;
};

struct max30102_data {
    struct i2c_client *client;
    struct regmap *regmap;
    struct timer_list poll_timer;
    struct work_struct poll_work;
    DECLARE_KFIFO(data_fifo, struct sample_data, KFIFO_SIZE);
    wait_queue_head_t read_wait;
    struct miscdevice misc_dev;
    bool device_opened;
    spinlock_t fifo_lock;

    unsigned long last_temp_tick;
    int chip_temp_int;
    u8 chip_temp_frac;
};

/* ============ 温度读取 ============ */
static void max30102_update_temp(struct max30102_data *data)
{
    unsigned int val;
    regmap_write(data->regmap, REG_TEMP_CONFIG, 0x01);
    msleep(30);
    regmap_read(data->regmap, REG_TEMP_INT, &val);
    data->chip_temp_int = (int8_t)val;
    regmap_read(data->regmap, REG_TEMP_FRAC, &val);
    data->chip_temp_frac = val & 0x0F;
}

/* ============ 重置 FIFO ============ */
static void max30102_reset_fifo(struct max30102_data *data)
{
    regmap_write(data->regmap, REG_FIFO_WR_PTR, 0x00);
    regmap_write(data->regmap, REG_OVF_COUNTER, 0x00);
    regmap_write(data->regmap, REG_FIFO_RD_PTR, 0x00);
    /* 重新使能采样模式 */
    regmap_write(data->regmap, REG_MODE_CONFIG, 0x03);
    dev_info(&data->client->dev, "FIFO reset done\n");
}

/* ============ 核心工作函数 ============ */
static void max30102_poll_work(struct work_struct *work)
{
    struct max30102_data *data = container_of(work, struct max30102_data, poll_work);
    unsigned int reg_val;
    u8 wr_ptr, rd_ptr, ovf;
    int available, i, ret;
    u8 sample_buf[6];
    struct sample_data sample;
    unsigned long flags;

    /* ----- 检测 FIFO 溢出 ----- */
    regmap_read(data->regmap, REG_OVF_COUNTER, &reg_val);
    ovf = reg_val & 0x1F;
    if (ovf) {
        dev_warn(&data->client->dev, "FIFO overflow: %d samples lost, resetting...\n", ovf);
        max30102_reset_fifo(data);
        /* 跳过本次采集，重新启动定时器 */
        mod_timer(&data->poll_timer, jiffies + SAMPLE_PERIOD);
        return;
    }

    /* ----- 每秒更新一次温度 ----- */
    if (jiffies - data->last_temp_tick > msecs_to_jiffies(1000)) {
        max30102_update_temp(data);
        data->last_temp_tick = jiffies;
    }

    /* ----- 读取 FIFO 指针 ----- */
    regmap_read(data->regmap, REG_FIFO_WR_PTR, &reg_val);
    wr_ptr = reg_val & 0x1F;
    regmap_read(data->regmap, REG_FIFO_RD_PTR, &reg_val);
    rd_ptr = reg_val & 0x1F;

    available = (wr_ptr - rd_ptr) & 0x1F;
    if (available <= 0)
        goto restart_timer;

    /* ----- 读取 FIFO 数据 ----- */
    for (i = 0; i < available; i++) {
        ret = regmap_bulk_read(data->regmap, REG_FIFO_DATA, sample_buf, 6);
        if (ret != 0) {
            dev_err(&data->client->dev, "FIFO read failed: %d\n", ret);
            break;
        }
        sample.red = ((uint32_t)sample_buf[0] << 16) |
                     ((uint32_t)sample_buf[1] << 8) |
                     sample_buf[2];
        sample.red &= 0x3FFFF;
        sample.ir  = ((uint32_t)sample_buf[3] << 16) |
                     ((uint32_t)sample_buf[4] << 8) |
                     sample_buf[5];
        sample.ir  &= 0x3FFFF;

        spin_lock_irqsave(&data->fifo_lock, flags);
        if (!kfifo_is_full(&data->data_fifo))
            kfifo_put(&data->data_fifo, sample);
        spin_unlock_irqrestore(&data->fifo_lock, flags);
    }
    wake_up_interruptible(&data->read_wait);

restart_timer:
    mod_timer(&data->poll_timer, jiffies + SAMPLE_PERIOD);
}

/* ============ 定时器回调 ============ */
static void max30102_timer_handler(unsigned long arg)
{
    struct max30102_data *data = (struct max30102_data *)arg;
    schedule_work(&data->poll_work);
}

/* ============ 打开设备 ============ */
static int max30102_open(struct inode *inode, struct file *filp)
{
    struct max30102_data *data = container_of(filp->private_data,
                                  struct max30102_data, misc_dev);
    if (data->device_opened)
        return -EBUSY;

    data->device_opened = true;
    filp->private_data = data;
    kfifo_reset(&data->data_fifo);

    /* 清空 FIFO，确保干净启动 */
    max30102_reset_fifo(data);

    data->last_temp_tick = jiffies;
    max30102_update_temp(data);

    mod_timer(&data->poll_timer, jiffies + SAMPLE_PERIOD);
    dev_info(&data->client->dev, "device opened\n");
    return 0;
}

/* ============ 释放设备 ============ */
static int max30102_release(struct inode *inode, struct file *filp)
{
    struct max30102_data *data = filp->private_data;
    del_timer_sync(&data->poll_timer);
    flush_work(&data->poll_work);
    kfifo_reset(&data->data_fifo);
    data->device_opened = false;
    /* 停止采样，降低功耗 */
    regmap_write(data->regmap, REG_MODE_CONFIG, 0x00);
    dev_info(&data->client->dev, "device released\n");
    return 0;
}

/* ============ 读取设备 ============ */
static ssize_t max30102_read(struct file *filp, char __user *buf,
                             size_t count, loff_t *ppos)
{
    struct max30102_data *data = filp->private_data;
    struct sample_data sample;
    int ret;
    unsigned long flags;
    char out_buf[64];
    int len;

    if (kfifo_is_empty(&data->data_fifo)) {
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        ret = wait_event_interruptible(data->read_wait,
                                       !kfifo_is_empty(&data->data_fifo));
        if (ret)
            return -ERESTARTSYS;
    }

    spin_lock_irqsave(&data->fifo_lock, flags);
    ret = kfifo_get(&data->data_fifo, &sample);
    spin_unlock_irqrestore(&data->fifo_lock, flags);
    if (!ret)
        return -EAGAIN;

    len = snprintf(out_buf, sizeof(out_buf), "%u %u %d.%02d\n",
                   sample.red, sample.ir,
                   data->chip_temp_int, data->chip_temp_frac * 6);
    if (count < len)
        return -EINVAL;
    if (copy_to_user(buf, out_buf, len))
        return -EFAULT;
    return len;
}

/* ============ 文件操作结构体 ============ */
static const struct file_operations max30102_fops = {
    .owner   = THIS_MODULE,
    .open    = max30102_open,
    .release = max30102_release,
    .read    = max30102_read,
};

/* ============ regmap 配置 ============ */
static const struct regmap_config max30102_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = 0x2F,
};

/* ============ 芯片初始化 ============ */
static int max30102_hw_init(struct max30102_data *data)
{
    unsigned int val;

    /* 软复位 */
    regmap_write(data->regmap, REG_MODE_CONFIG, 0x40);
    msleep(20);

    /* 读取芯片 ID 验证 */
    regmap_read(data->regmap, REG_PART_ID, &val);
    dev_info(&data->client->dev, "Part ID: 0x%02x\n", val);
    if (val != 0x15) {
        dev_warn(&data->client->dev, "Unexpected Part ID: 0x%02x\n", val);
    }

    /* 配置 FIFO：关闭滚动模式，避免覆盖 */
    regmap_write(data->regmap, REG_FIFO_CONFIG, 0x00);

    /* 配置采样率：100Hz, 18bit, 400us */
    regmap_write(data->regmap, REG_SPO2_CONFIG, 0x27);

    /* LED 电流 */
    regmap_write(data->regmap, REG_LED1_PA, 0x25);
    regmap_write(data->regmap, REG_LED2_PA, 0x25);

    /* 清空 FIFO */
    max30102_reset_fifo(data);

    return 0;
}

/* ============ Probe ============ */
static int max30102_probe(struct i2c_client *client,
                          const struct i2c_device_id *id)
{
    int ret;
    struct max30102_data *data;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);

    data->regmap = devm_regmap_init_i2c(client, &max30102_regmap_config);
    if (IS_ERR(data->regmap))
        return PTR_ERR(data->regmap);

    INIT_KFIFO(data->data_fifo);
    spin_lock_init(&data->fifo_lock);
    init_waitqueue_head(&data->read_wait);
    INIT_WORK(&data->poll_work, max30102_poll_work);
    setup_timer(&data->poll_timer, max30102_timer_handler, (unsigned long)data);

    ret = max30102_hw_init(data);
    if (ret) {
        dev_err(&client->dev, "Hardware init failed\n");
        return ret;
    }

    data->misc_dev.name = "max30102";
    data->misc_dev.minor = MISC_DYNAMIC_MINOR;
    data->misc_dev.fops = &max30102_fops;

    ret = misc_register(&data->misc_dev);
    if (ret) {
        del_timer_sync(&data->poll_timer);
        dev_err(&client->dev, "misc_register failed\n");
        return ret;
    }

    dev_info(&client->dev, "MAX30102 loaded /dev/max30102\n");
    return 0;
}

/* ============ Remove ============ */
static int max30102_remove(struct i2c_client *client)
{
    struct max30102_data *data = i2c_get_clientdata(client);
    misc_deregister(&data->misc_dev);
    del_timer_sync(&data->poll_timer);
    flush_work(&data->poll_work);
    regmap_write(data->regmap, REG_MODE_CONFIG, 0x00);
    dev_info(&client->dev, "MAX30102 removed\n");
    return 0;
}

/* ============ 设备树匹配 ============ */
static const struct i2c_device_id max30102_id[] = {
    { "max30102", 0 },
    {}
};
MODULE_DEVICE_TABLE(i2c, max30102_id);

static const struct of_device_id max30102_of_match[] = {
    { .compatible = "maxim,max30102" },
    {}
};
MODULE_DEVICE_TABLE(of, max30102_of_match);

/* ============ I2C 驱动 ============ */
static struct i2c_driver max30102_driver = {
    .driver = {
        .name = "max30102_misc",
        .of_match_table = max30102_of_match,
    },
    .probe = max30102_probe,
    .remove = max30102_remove,
    .id_table = max30102_id,
};

module_i2c_driver(max30102_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("yu");
MODULE_DESCRIPTION("MAX30102 driver with FIFO overflow recovery");