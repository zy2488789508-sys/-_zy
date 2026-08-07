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
#define MPU_WHO_AM_I_REG 0x75
#define MPU_PWR_MGMT_1 0x6B
#define MPU_ACCEL_XOUT_H 0x3B
#define MPU_GYRO_XOUT_H 0x43
#define MPU_GYRO_CONFIG 0x1B
#define MPU_ACCEL_CONFIG 0x1C
#define SAMPLE_PERIOD msecs_to_jiffies(10)
#define KFIFO_SIZE 256
struct mpu_sample {
int16_t accel_x;
int16_t accel_y;
int16_t accel_z;
int16_t gyro_x;
int16_t gyro_y;
int16_t gyro_z;
};
struct mpu6500_data {
struct i2c_client *client;
struct regmap *regmap;
struct timer_list poll_timer;
struct work_struct poll_work;
DECLARE_KFIFO(data_fifo, struct mpu_sample, KFIFO_SIZE);
wait_queue_head_t read_wait;
struct miscdevice misc_dev;
bool device_opened;
spinlock_t fifo_lock;
};
static u8 mpu_reg_read(struct mpu6500_data *data, u8 reg)
{
unsigned int val;
int ret;
ret = regmap_read(data->regmap, reg, &val);
if (ret)
{
dev_err(&data->client->dev, "reg 0x%02x read err, ret=%d\n", reg, ret);
return 0;
}
return (u8)val;
}
static int mpu_reg_write(struct mpu6500_data *data, u8 reg, u8 val)
{
int ret;
ret = regmap_write(data->regmap, reg, val);
if (ret)
{
dev_err(&data->client->dev, "reg 0x%02x write err, ret=%d\n", reg, ret);
}
return ret;
}
static void mpu6500_poll_work(struct work_struct *work)
{
struct mpu6500_data *data = container_of(work, struct mpu6500_data, poll_work);
u8 raw_buf[12] = {0};
struct mpu_sample sample;
int ret;
unsigned long flags;
ret = regmap_bulk_read(data->regmap, MPU_ACCEL_XOUT_H, raw_buf, 12);
if (ret != 0)
goto restart_timer;
sample.accel_x = (int16_t)((raw_buf[0] << 8) | raw_buf[1]);
sample.accel_y = (int16_t)((raw_buf[2] << 8) | raw_buf[3]);
sample.accel_z = (int16_t)((raw_buf[4] << 8) | raw_buf[5]);
sample.gyro_x = (int16_t)((raw_buf[6] << 8) | raw_buf[7]);
sample.gyro_y = (int16_t)((raw_buf[8] << 8) | raw_buf[9]);
sample.gyro_z = (int16_t)((raw_buf[10] << 8) | raw_buf[11]);
spin_lock_irqsave(&data->fifo_lock, flags);
if (!kfifo_is_full(&data->data_fifo))
kfifo_put(&data->data_fifo, sample);
spin_unlock_irqrestore(&data->fifo_lock, flags);
wake_up_interruptible(&data->read_wait);
restart_timer:
mod_timer(&data->poll_timer, jiffies + SAMPLE_PERIOD);
}
static void mpu6500_timer_handler(unsigned long arg)
{
struct mpu6500_data *data = (struct mpu6500_data *)arg;
schedule_work(&data->poll_work);
}
static int mpu6500_open(struct inode *inode, struct file *filp)
{
struct mpu6500_data *data = container_of(filp->private_data, struct mpu6500_data, misc_dev);
if (data->device_opened)
return -EBUSY;
data->device_opened = true;
filp->private_data = data;
kfifo_reset(&data->data_fifo);
mod_timer(&data->poll_timer, jiffies + SAMPLE_PERIOD);
return 0;
}
static int mpu6500_release(struct inode *inode, struct file *filp)
{
struct mpu6500_data *data = filp->private_data;
del_timer_sync(&data->poll_timer);
flush_work(&data->poll_work);
kfifo_reset(&data->data_fifo);
data->device_opened = false;
return 0;
}
static ssize_t mpu6500_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
struct mpu6500_data *data = filp->private_data;
struct mpu_sample sample;
int ret;
unsigned long flags;
char out_buf[128];
int len;
if (kfifo_is_empty(&data->data_fifo))
{
if (filp->f_flags & O_NONBLOCK)
return -EAGAIN;
ret = wait_event_interruptible(data->read_wait, !kfifo_is_empty(&data->data_fifo));
if (ret)
return -ERESTARTSYS;
}
spin_lock_irqsave(&data->fifo_lock, flags);
ret = kfifo_get(&data->data_fifo, &sample);
spin_unlock_irqrestore(&data->fifo_lock, flags);
if (!ret)
return -EAGAIN;
len = snprintf(out_buf, sizeof(out_buf),
"ax:%d ay:%d az:%d gx:%d gy:%d gz:%d\n",
sample.accel_x, sample.accel_y, sample.accel_z,
sample.gyro_x, sample.gyro_y, sample.gyro_z);
if (count < len)
return -EINVAL;
if (copy_to_user(buf, out_buf, len))
return -EFAULT;
return len;
}
static const struct file_operations mpu6500_fops = {
.owner = THIS_MODULE,
.open = mpu6500_open,
.release = mpu6500_release,
.read = mpu6500_read,
};
static const struct regmap_config mpu6500_regmap_config = {
.reg_bits = 8,
.val_bits = 8,
.max_register = 0x7F,
};
static int mpu6500_hw_init(struct mpu6500_data *data)
{
int ret;
ret = mpu_reg_write(data, MPU_PWR_MGMT_1, 0x00);
if (ret)
return ret;
msleep(100);
ret = mpu_reg_write(data, MPU_GYRO_CONFIG, 0x00);
ret |= mpu_reg_write(data, MPU_ACCEL_CONFIG, 0x00);
return ret;
}
static int mpu6500_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
int ret = 0;
u8 chip_id;
struct mpu6500_data *data;
data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
if (!data)
return -ENOMEM;
data->client = client;
i2c_set_clientdata(client, data);
data->regmap = devm_regmap_init_i2c(client, &mpu6500_regmap_config);
if (IS_ERR(data->regmap))
return PTR_ERR(data->regmap);
INIT_KFIFO(data->data_fifo);
spin_lock_init(&data->fifo_lock);
init_waitqueue_head(&data->read_wait);
INIT_WORK(&data->poll_work, mpu6500_poll_work);
setup_timer(&data->poll_timer, mpu6500_timer_handler, (unsigned long)data);
chip_id = mpu_reg_read(data, MPU_WHO_AM_I_REG);
if (chip_id != 0x73 && chip_id != 0x70)
{
dev_err(&client->dev, "invalid chip id:0x%02x, not mpu6500\n", chip_id);
ret = -ENODEV;
goto err_id;
}
dev_info(&client->dev, "mpu hardware chip id:%#x\n", chip_id);
ret = mpu6500_hw_init(data);
if (ret)
{
dev_err(&client->dev, "mpu hardware init failed\n");
goto err_init;
}
data->misc_dev.name = "mpu6500";
data->misc_dev.minor = MISC_DYNAMIC_MINOR;
data->misc_dev.fops = &mpu6500_fops;
ret = misc_register(&data->misc_dev);
if (ret)
{
del_timer_sync(&data->poll_timer);
goto err_misc;
}
dev_info(&client->dev, "MPU loaded /dev/mpu6500\n");
return 0;
err_misc:
err_init:
err_id:
return ret;
}
static int mpu6500_remove(struct i2c_client *client)
{
 struct mpu6500_data *data = i2c_get_clientdata(client);
    /* 2. 等待所有已调度的工作完 */
    flush_work(&data->poll_work);

    /* 3. 让MPU进入休眠 */
    regmap_write(data->regmap, MPU_PWR_MGMT_1, 0x40);

    /* 4. 注销 misc 设备，阻止用户空间访问 */
    misc_deregister(&data->misc_dev);
    dev_info(&client->dev, "mpu remove success\n");
    return 0;
}
// ========== compatible = "my,mpu9250" ==========
static const struct i2c_device_id mpu6500_id[] =
{{ "my,mpu9250", 0 }, {}};
MODULE_DEVICE_TABLE(i2c, mpu6500_id);
static const struct of_device_id mpu6500_of_match[] =
{{ .compatible = "my,mpu9250" }, {}};
MODULE_DEVICE_TABLE(of, mpu6500_of_match);
static struct i2c_driver mpu6500_driver = {
.driver = {
.name = "mpu9250_misc",
.of_match_table = mpu6500_of_match,
},
.probe = mpu6500_probe,
.remove = mpu6500_remove,
.id_table = mpu6500_id,
};
module_i2c_driver (mpu6500_driver);
MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION ("MPU6500 driver match dts my,mpu9250");
MODULE_AUTHOR ("yu");