#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include "algorithm.h"

#define CHAR_DEV_PATH "/dev/max30102"
#define HR_LOW_LIMIT  40
#define HR_HIGH_LIMIT 180

static uint32_t ir_buf[BUFFER_SIZE];
static uint32_t red_buf[BUFFER_SIZE];
static int buf_idx = 0;

// 环形缓存，固定存7个有效心率
static int hr_history_buf[HR_FIFO_SIZE];
static int hr_fill_num = 0; // 当前缓存存了多少个有效值
static int hr_wr_pos = 0;

static int get_array_average(int *arr, int len)
{
    if(len <= 0) return -1;
    int sum = 0;
    for (int i = 0; i < len; i++)
        sum += arr[i];
    return sum / len;
}

int main(void)
{
    int fd = open(CHAR_DEV_PATH, O_RDONLY);
    if (fd < 0)
    {
        perror("打开/dev/max30102失败");
        return -1;
    }

    char recv_buf[64];
    uint32_t red_raw, ir_raw;
    int temp_int, temp_frac;
    float chip_temp;

    int32_t spo2_result, hr_result;
    int8_t spo2_valid_flag, hr_valid_flag;

    printf("===== MAX30102 =====\n");
    printf("红光值 | 红外值 | SpO2血氧 | 心率BPM | 芯片温度\n");

    while (1)
    {
        memset(recv_buf, 0, sizeof(recv_buf));
        ssize_t read_len = read(fd, recv_buf, sizeof(recv_buf) - 1);
        if (read_len <= 0) continue;

        if (sscanf(recv_buf, "%u %u %d.%d", &red_raw, &ir_raw, &temp_int, &temp_frac) != 4)
            continue;

        chip_temp = temp_int + temp_frac * 0.0625f;
        red_buf[buf_idx] = red_raw;
        ir_buf[buf_idx] = ir_raw;
        buf_idx++;

        if (buf_idx >= BUFFER_SIZE)
        {
            maxim_heart_rate_and_oxygen_saturation(ir_buf, BUFFER_SIZE, red_buf,
                                                   &spo2_result, &spo2_valid_flag,
                                                   &hr_result, &hr_valid_flag);

            int final_heartrate = -1;
            int final_hr_ok = 0;

            // 合法心率存入环形缓存
            if (hr_valid_flag == 1 && hr_result >= HR_LOW_LIMIT && hr_result <= HR_HIGH_LIMIT)
            {
                hr_history_buf[hr_wr_pos] = hr_result;
                hr_wr_pos = (hr_wr_pos + 1) % HR_FIFO_SIZE;
                if (hr_fill_num < HR_FIFO_SIZE)
                    hr_fill_num++;
            }

            // 只要缓存里有≥2个有效心率，就计算均值输出，不用硬等7个
            if (hr_fill_num >= 2)
            {
                final_heartrate = get_array_average(hr_history_buf, hr_fill_num);
                final_hr_ok = 1;
            }

            // 打印
            printf("%6d | %6d | ", red_raw, ir_raw);
            spo2_valid_flag ? printf("%3d%% | ", spo2_result) : printf(" --- | ");
            final_hr_ok ? printf("%3d | ", final_heartrate) : printf(" --- | ");
            printf("%.2f℃\n", chip_temp);

            buf_idx = 0;
        }
    }

    close(fd);
    return 0;
}
