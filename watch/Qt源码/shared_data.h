#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <stdint.h>
#include <time.h>

typedef struct {
    // MPU6500
    float ax, ay, az;
    float gx, gy, gz;
    int mpu_updated;

    // GPS
    float lat, lon;
    float speed_kmh;
    float filtered_speed;
    int gps_fix;
    int gps_updated;

    // MAX30102
    int heart_rate;
    int spo2;
    float chip_temp;
    int hr_valid;
    int spo2_valid;

    // BMP280
    float bmp_temp;
    int bmp_press;
    int bmp_valid;

    // 卡尔曼缓冲
    float buf_acc_ms2;
    float buf_dt;

    // 摔倒检测
    uint8_t fall_detected;

    // 时间戳
    time_t timestamp;
} sensor_data_t;

#endif // SHARED_DATA_H
