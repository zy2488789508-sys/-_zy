#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "MQTTClient.h"

// ============ MAX30102 算法头文件 ============
#include "algorithm.h"

// ============ MQTT 配置参数 ============
#define MQTT_BROKER_ADDRESS "tcp://192.168.137.21:1883"
#define MQTT_CLIENT_ID     "imx6ull_fall_detector"
#define MQTT_TOPIC         "sensor/fall_alert"
#define MQTT_GPS_TOPIC     "sensor/gps"   // GPS 数据主题
#define MQTT_QOS           1
#define MQTT_TIMEOUT       10000L

// ============ 硬件参数 ============
#define ACC_SENS  16384.0f
#define GYRO_SENS 65.5f

#define GYRO_STILL_THRESH  4.5f
#define ACC_STILL_RANGE    0.28f
#define SPEED_NOISE_THRESH 1.3f
#define STILL_FRAME_CNT    5
#define MAX_ACC_POS_CTRL  1.2f
#define MAX_ACC_NEG_CTRL -0.7f
#define GPS_ZERO_CNT_THRESH 2
#define HPF_ALPHA 0.12f

// ============ MAX30102 参数 ============
#define MAX30102_BUFFER_SIZE 100
#define HR_FIFO_SIZE         7
#define HR_LOW_LIMIT         40
#define HR_HIGH_LIMIT        180

// ============ BMP280 IIO 路径 ============
#define BMP280_IIO_PATH "/sys/bus/iio/devices/iio:device0"

// ============ 蜂鸣器控制路径 ============
#define BEEP_BRIGHTNESS "/sys/class/leds/beep/brightness"

// ============ 一维卡尔曼结构体 ============
typedef struct {
    float x;
    float P;
    float Q;
    float R;
    float K;
} kalman_1d_t;

void kalman_init(kalman_1d_t *kf, float Q, float R) {
    kf->x = 0.0f;
    kf->P = 1.0f;
    kf->Q = Q;
    kf->R = R;
    kf->K = 0.0f;
}

float kalman_update(kalman_1d_t *kf, float control_input, float measurement, int has_measurement) {
    kf->x = kf->x + control_input;
    kf->P = kf->P + kf->Q;

    if (has_measurement) {
        kf->K = kf->P / (kf->P + kf->R);
        kf->x = kf->x + kf->K * (measurement - kf->x);
        kf->P = (1.0f - kf->K) * kf->P;
    }
    return kf->x;
}

// ============ 共享数据 ============
typedef struct {
    float ax, ay, az;
    float gx, gy, gz;
    int mpu_updated;

    float lat, lon;
    float speed_kmh;
    float filtered_speed;
    int gps_fix;
    int gps_updated;

    int heart_rate;
    int spo2;
    float chip_temp;
    int hr_valid;
    int spo2_valid;

    float bmp_temp;
    int bmp_press;
    int bmp_valid;

    float buf_acc_ms2;
    float buf_dt;
    uint8_t fall_detected;

    time_t timestamp; 
} sensor_data_t;

// 全局共享内存句柄与指针
static int shm_fd = -1;
static sensor_data_t *shared_data = NULL;

static kalman_1d_t speed_kf;
static struct timespec last_time;
static uint8_t gyro_still_count = 0;
static uint8_t acc_still_count = 0;
static uint8_t device_is_still = 0;
static uint8_t gps_zero_speed_cnt = 0;

float gx_offset = 0, gy_offset = 0, gz_offset = 0;
static float acc_hp_x = 0, acc_hp_y = 0, acc_hp_z = 0;

// ============ 蜂鸣器控制函数 ============
void beep_on(void) {
    int fd = open(BEEP_BRIGHTNESS, O_WRONLY);
    if (fd >= 0) {
        write(fd, "1", 1);
        close(fd);
    }
}

void beep_off(void) {
    int fd = open(BEEP_BRIGHTNESS, O_WRONLY);
    if (fd >= 0) {
        write(fd, "0", 1);
        close(fd);
    }
}

// ============ MQTT 摔倒报警发布函数 ============
void publish_fall_alert(void) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    char payload[100];

    int rc = MQTTClient_create(&client, MQTT_BROKER_ADDRESS, MQTT_CLIENT_ID,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("MQTT 客户端创建失败！rc=%d\n", rc);
        return;
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("MQTT 连接失败！rc=%d\n", rc);
        MQTTClient_destroy(&client);
        return;
    }

    snprintf(payload, sizeof(payload), 
             "{\"event\":\"fall_detected\",\"timestamp\":%ld,\"device\":\"%s\"}",
             time(NULL), MQTT_CLIENT_ID);
    pubmsg.payload = payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = MQTT_QOS;
    pubmsg.retained = 0;

    rc = MQTTClient_publishMessage(client, MQTT_TOPIC, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("MQTT 发布失败！rc=%d\n", rc);
    } else {
        MQTTClient_waitForCompletion(client, token, MQTT_TIMEOUT);
        printf("MQTT 消息已发送: %s\n", payload);
    }

    MQTTClient_disconnect(client, MQTT_TIMEOUT);
    MQTTClient_destroy(&client);
}

// ============ MQTT GPS 数据发布函数 ============
void publish_gps_data(float lat, float lon, float speed) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    char payload[200];

    int rc = MQTTClient_create(&client, MQTT_BROKER_ADDRESS, MQTT_CLIENT_ID,
                                MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("MQTT GPS: 创建失败！rc=%d\n", rc);
        return;
    }

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("MQTT GPS: 连接失败！rc=%d\n", rc);
        MQTTClient_destroy(&client);
        return;
    }

    snprintf(payload, sizeof(payload),
             "{\"type\":\"gps\",\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.2f,\"timestamp\":%ld}",
             lat, lon, speed, time(NULL));
    pubmsg.payload = payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = MQTT_QOS;
    pubmsg.retained = 0;

    rc = MQTTClient_publishMessage(client, MQTT_GPS_TOPIC, &pubmsg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        printf("MQTT GPS: 发布失败！rc=%d\n", rc);
    } else {
        MQTTClient_waitForCompletion(client, token, MQTT_TIMEOUT);
        printf("MQTT GPS: 已发送 (%.6f, %.6f) 速度 %.2f km/h\n", lat, lon, speed);
    }

    MQTTClient_disconnect(client, MQTT_TIMEOUT);
    MQTTClient_destroy(&client);
}

// ============ 工具函数 ============
float get_delta_time(struct timespec *last) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float dt = (now.tv_sec - last->tv_sec) + (now.tv_nsec - last->tv_nsec) / 1000000000.0f;
    if (dt < 0.001f) dt = 0.001f;
    if (dt > 0.1f) dt = 0.1f;
    *last = now;
    return dt;
}

// ============ 线程1：MPU6500（含摔倒检测 + 姿态角 + 两种清除） ============
void *mpu_thread_func(void *arg) {
    FILE *fp = fopen("/dev/mpu6500", "r");
    if (!fp) {
        perror("open /dev/mpu6500 fail");
        return NULL;
    }

    char buf[128];
    int16_t raw_ax, raw_ay, raw_az;
    int16_t raw_gx, raw_gy, raw_gz;
    float dt;

    #define FALL_STATE_IDLE      0
    #define FALL_STATE_IMPACT    1
    #define FALL_STATE_STILL     2

    static uint8_t fall_state = FALL_STATE_IDLE;
    static uint8_t fall_frame_count = 0;

    // ===== 姿态角初始化相关 =====
    static uint8_t init_pose_flag = 1;
    static float init_pitch = 0.0f, init_roll = 0.0f;
    static int pose_calib_cnt = 0;
    static float pitch_sum = 0.0f, roll_sum = 0.0f;

    // ===== 回正连续计数器 =====
    static uint8_t rect_count = 0;

    while (1) {
        memset(buf, 0, sizeof(buf));
        if (fgets(buf, sizeof(buf), fp) == NULL) {
            usleep(10000);
            continue;
        }
        sscanf(buf, "ax:%hd ay:%hd az:%hd gx:%hd gy:%hd gz:%hd",
               &raw_ax, &raw_ay, &raw_az, &raw_gx, &raw_gy, &raw_gz);

        dt = get_delta_time(&last_time);

        // 写入共享内存
        shared_data->ax = raw_ax / ACC_SENS;
        shared_data->ay = raw_ay / ACC_SENS;
        shared_data->az = raw_az / ACC_SENS;
        shared_data->gx = (raw_gx - gx_offset) / GYRO_SENS;
        shared_data->gy = (raw_gy - gy_offset) / GYRO_SENS;
        shared_data->gz = (raw_gz - gz_offset) / GYRO_SENS;
        shared_data->mpu_updated = 1;

        // --- 静止检测 ---
        int gyro_current_still = (fabs(shared_data->gx) < GYRO_STILL_THRESH)
                              && (fabs(shared_data->gy) < GYRO_STILL_THRESH)
                              && (fabs(shared_data->gz) < GYRO_STILL_THRESH);

        float acc_norm = sqrtf(shared_data->ax*shared_data->ax + shared_data->ay*shared_data->ay + shared_data->az*shared_data->az);
        int acc_current_still = (fabs(acc_norm - 1.0f) < ACC_STILL_RANGE)
                             && fabs(shared_data->ax) < ACC_STILL_RANGE
                             && fabs(shared_data->ay) < ACC_STILL_RANGE;

        if(gyro_current_still) {
            gyro_still_count++;
            acc_still_count=0;
        } else if(acc_current_still) {
            acc_still_count++;
            gyro_still_count=0;
        } else {
            gyro_still_count=0;
            acc_still_count=0;
        }

        if(gyro_still_count >= STILL_FRAME_CNT || acc_still_count >= STILL_FRAME_CNT) {
            device_is_still = 1;
            speed_kf.x = 0.0f;
            speed_kf.P = 0.005f;
            shared_data->filtered_speed = 0.0f;
            shared_data->speed_kmh = 0.0f;
        } else {
            device_is_still = 0;
        }

        // --- 高通滤波 ---
        acc_hp_x = HPF_ALPHA * shared_data->ax + (1-HPF_ALPHA)*acc_hp_x;
        acc_hp_y = HPF_ALPHA * shared_data->ay + (1-HPF_ALPHA)*acc_hp_y;
        acc_hp_z = HPF_ALPHA * shared_data->az + (1-HPF_ALPHA)*acc_hp_z;

        float acc_h_xy = sqrtf(acc_hp_x*acc_hp_x + acc_hp_y*acc_hp_y);
        float acc_dir = acc_hp_x > 0 ? 1.0f : -1.0f;
        float acc_ms2 = acc_h_xy * 9.8f * acc_dir;
        if (fabs(acc_ms2) > 25) acc_ms2 = 0;
        shared_data->buf_acc_ms2 = acc_ms2;
        shared_data->buf_dt = dt;

        // ===== 计算姿态角 =====
        float pitch = atan2(-shared_data->ax, sqrt(shared_data->ay*shared_data->ay + shared_data->az*shared_data->az)) * 57.2958f;
        float roll  = atan2(shared_data->ay, shared_data->az) * 57.2958f;

        // 校准初始姿态（取前50帧平均值）
        if (init_pose_flag) {
            pitch_sum += pitch;
            roll_sum  += roll;
            pose_calib_cnt++;
            if (pose_calib_cnt >= 50) {
                init_pitch = pitch_sum / 50;
                init_roll  = roll_sum / 50;
                init_pose_flag = 0;
                printf("[姿态校准] init_pitch=%.1f, init_roll=%.1f\n", init_pitch, init_roll);
            }
        }

        // 计算姿态变化量（仅当校准完成后）
        float pitch_change = 0.0f, roll_change = 0.0f;
        if (!init_pose_flag) {
            pitch_change = fabs(pitch - init_pitch);
            roll_change  = fabs(roll - init_roll);
        }

        // ===== 摔倒检测（两种触发 + 两种清除） =====
        switch (fall_state) {
            case FALL_STATE_IDLE:
                // === 触发方式1：冲击检测 ===
                if (acc_norm > 1.8f) {
                    fall_frame_count++;
                    if (fall_frame_count >= 1) {
                        fall_state = FALL_STATE_IMPACT;
                        fall_frame_count = 0;
                        printf("[冲击触发] acc_norm=%.2fg\n", acc_norm);
                        rect_count = 0;
                    }
                }
                // === 触发方式2：姿态异常 ===
                else if (!init_pose_flag && (pitch_change > 50.0f || roll_change > 50.0f)) {
                    fall_frame_count++;
                    if (fall_frame_count >= 1) {
                        fall_state = FALL_STATE_IMPACT;
                        fall_frame_count = 0;
                        printf("[姿态触发] pitch_change=%.1f, roll_change=%.1f\n", pitch_change, roll_change);
                        rect_count = 0;
                    }
                } else {
                    fall_frame_count = 0;
                }
                break;

            case FALL_STATE_IMPACT:
                // 检测静止
                if (acc_norm > 0.5f && acc_norm < 1.5f) {
                    fall_frame_count++;
                    if (fall_frame_count >= 3) {
                        fall_state = FALL_STATE_STILL;
                        fall_frame_count = 0;
                        shared_data->fall_detected = 1;
                        printf("⚠️⚠️⚠️ 摔倒报警触发！⚠️⚠️⚠️\n");
                        beep_on();
                        publish_fall_alert();
                        rect_count = 0;
                    }
                } else {
                    fall_frame_count = 0;
                    if (++fall_frame_count > 20) {
                        fall_state = FALL_STATE_IDLE;
                        fall_frame_count = 0;
                        printf("[超时复位] 未检测到静止，状态复位\n");
                    }
                }
                break;

            case FALL_STATE_STILL:
                // === 清除方式1：回正检测（检测竖立状态） ===
                if (fabs(acc_norm - 1.0f) < ACC_STILL_RANGE &&
                    fabs(shared_data->ax) < 0.3f &&
                    fabs(shared_data->ay) < 0.3f &&
                    fabs(shared_data->az) > 0.85f) {
                    rect_count++;
                    if (rect_count >= 10) {
                        fall_state = FALL_STATE_IDLE;
                        fall_frame_count = 0;
                        shared_data->fall_detected = 0;
                        rect_count = 0;
                        
                        // 重置姿态基准，防止回正后立即再次触发
                        init_pose_flag = 1;
                        pose_calib_cnt = 0;
                        pitch_sum = 0.0f;
                        roll_sum = 0.0f;
                        
                        beep_off();
                        printf("[回正清除] 设备已稳定回正，报警清除，重新校准姿态\n");
                    }
                } else {
                    rect_count = 0;
                }

                // === 清除方式2：持续移动 ===
                if (acc_norm < 0.5f || acc_norm > 1.5f) {
                    fall_frame_count++;
                    if (fall_frame_count >= 5) {
                        fall_state = FALL_STATE_IDLE;
                        fall_frame_count = 0;
                        shared_data->fall_detected = 0;
                        beep_off();
                        printf("[移动清除] 用户已起身移动，报警清除\n");
                    }
                } else {
                    fall_frame_count = 0;
                }
                break;

            default:
                fall_state = FALL_STATE_IDLE;
                break;
        }

        usleep(50000);  // 20Hz
    }

    fclose(fp);
    return NULL;
}

// ============ 线程2：GPS ============
void *gps_thread_func(void *arg) {
    FILE *fp = fopen("/dev/ttymxc1", "r");
    if (!fp) {
        perror("open GPS /dev/ttymxc1 fail");
        return NULL;
    }

    char buf[256];
    while (1) {
        memset(buf, 0, sizeof(buf));
        if (fgets(buf, sizeof(buf), fp) == NULL) {
            usleep(100000);
            continue;
        }

        if (strncmp(buf, "$GPRMC", 6) == 0 || strncmp(buf, "$GNRMC", 6) == 0) {
            char valid_flag;
            float lat_dm, lon_dm, speed_knot;
            char lat_dir[2], lon_dir[2];
            int ret = sscanf(buf, "%*[^,],%*[^,],%c,%f,%1c,%f,%1c,%f",
                             &valid_flag, &lat_dm, lat_dir, &lon_dm, lon_dir, &speed_knot);
            if (ret != 6) continue;

            uint8_t local_still = device_is_still;
            float acc_buf = shared_data->buf_acc_ms2;
            float dt_buf = shared_data->buf_dt;

            if (valid_flag == 'A') {
                float lat_deg = (int)(lat_dm / 100.0f);
                float lat_min = lat_dm - lat_deg * 100.0f;
                float lat = lat_deg + lat_min / 60.0f;
                if (lat_dir[0] == 'S') lat = -lat;

                float lon_deg = (int)(lon_dm / 100.0f);
                float lon_min = lon_dm - lon_deg * 100.0f;
                float lon = lon_deg + lon_min / 60.0f;
                if (lon_dir[0] == 'W') lon = -lon;

                float speed_kmh = speed_knot * 1.852f;

                if(speed_kmh < SPEED_NOISE_THRESH) {
                    gps_zero_speed_cnt++;
                    if(gps_zero_speed_cnt >= GPS_ZERO_CNT_THRESH) {
                        speed_kf.x = 0;
                        speed_kf.P = 0.005f;
                        shared_data->filtered_speed = 0;
                        shared_data->speed_kmh = 0;
                    }
                } else {
                    gps_zero_speed_cnt = 0;
                }

                if (local_still) {
                    shared_data->lat = lat;
                    shared_data->lon = lon;
                    shared_data->speed_kmh = 0.0f;
                    shared_data->gps_fix = 1;
                    shared_data->gps_updated = 1;
                    continue;
                }

                if (speed_kmh < SPEED_NOISE_THRESH) speed_kmh = 0.0f;
                float measure_ms = speed_kmh / 3.6f;
                float ctrl = acc_buf * dt_buf;

                if (ctrl > 0) {
                    if (ctrl > MAX_ACC_POS_CTRL) ctrl = MAX_ACC_POS_CTRL;
                } else {
                    if (ctrl < MAX_ACC_NEG_CTRL) ctrl = MAX_ACC_NEG_CTRL;
                }

                float speed_ms = kalman_update(&speed_kf, ctrl, measure_ms, 1);
                shared_data->filtered_speed = speed_ms * 3.6f;

                shared_data->lat = lat;
                shared_data->lon = lon;
                shared_data->speed_kmh = speed_kmh;
                shared_data->gps_fix = 1;
                shared_data->gps_updated = 1;
            } else {
                shared_data->gps_fix = 0;
                shared_data->gps_updated = 1;
            }
        }
    }
    fclose(fp);
    return NULL;
}

// ============ 线程3：MAX30102 ============
void *max30102_thread_func(void *arg) {
    int fd;
    int retry_count = 0;
    const int MAX_RETRY = 10;

    while (1) {
        fd = open("/dev/max30102", O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            perror("open /dev/max30102 fail, retrying...");
            sleep(1);
            continue;
        }
        printf("MAX30102 设备打开成功 (非阻塞模式)\n");

        char recv_buf[64];
        uint32_t red_raw, ir_raw;
        int temp_int, temp_frac;
        float chip_temp;
        int i;

        static uint32_t ir_buf[MAX30102_BUFFER_SIZE];
        static uint32_t red_buf[MAX30102_BUFFER_SIZE];
        static int buf_idx = 0;

        static int hr_history_buf[HR_FIFO_SIZE];
        static int hr_fill_num = 0;
        static int hr_wr_pos = 0;

        time_t last_valid_time = time(NULL);
        int no_data_count = 0;

        while (1) {
            memset(recv_buf, 0, sizeof(recv_buf));
            ssize_t read_len = read(fd, recv_buf, sizeof(recv_buf) - 1);

            if (read_len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    usleep(10000);
                    continue;
                }
                perror("read max30102 error");
                retry_count++;
                if (retry_count >= MAX_RETRY) {
                    printf("连续读取错误，重新打开设备\n");
                    close(fd);
                    break;
                }
                usleep(100000);
                continue;
            }

            if (read_len == 0) {
                printf("read returned 0, re-opening\n");
                close(fd);
                break;
            }

            retry_count = 0;
            no_data_count = 0;
            last_valid_time = time(NULL);

            if (sscanf(recv_buf, "%u %u %d.%d", &red_raw, &ir_raw, &temp_int, &temp_frac) != 4) {
                continue;
            }

            chip_temp = temp_int + temp_frac * 0.0625f;

            red_buf[buf_idx] = red_raw;
            ir_buf[buf_idx] = ir_raw;
            buf_idx++;

            if (buf_idx >= MAX30102_BUFFER_SIZE) {
                int32_t spo2_result = 0, hr_result = 0;
                int8_t spo2_valid = 0, hr_valid = 0;

                maxim_heart_rate_and_oxygen_saturation(ir_buf, MAX30102_BUFFER_SIZE,
                                                       red_buf,
                                                       &spo2_result, &spo2_valid,
                                                       &hr_result, &hr_valid);

                shared_data->chip_temp = chip_temp;

                if (hr_valid == 1 && hr_result >= HR_LOW_LIMIT && hr_result <= HR_HIGH_LIMIT) {
                    hr_history_buf[hr_wr_pos] = (int)hr_result;
                    hr_wr_pos = (hr_wr_pos + 1) % HR_FIFO_SIZE;
                    if (hr_fill_num < HR_FIFO_SIZE) hr_fill_num++;
                }

                if (hr_fill_num >= 2) {
                    int sum = 0;
                    for (i = 0; i < hr_fill_num; i++) sum += hr_history_buf[i];
                    shared_data->heart_rate = sum / hr_fill_num;
                    shared_data->hr_valid = 1;
                } else {
                    shared_data->hr_valid = 0;
                    shared_data->heart_rate = 0;
                }

                if (spo2_valid == 1) {
                    shared_data->spo2 = (int)spo2_result;
                    shared_data->spo2_valid = 1;
                } else {
                    shared_data->spo2_valid = 0;
                    shared_data->spo2 = 0;
                }

                buf_idx = 0;
            }

            usleep(20000);
        }

        close(fd);
        sleep(1);
    }

    return NULL;
}

// ============ 线程4：BMP280 ============
void *bmp280_thread_func(void *arg) {
    char path[128];
    FILE *fp;
    int temp_raw, press_raw;

    while (1) {
        snprintf(path, sizeof(path), "%s/in_temp_raw", BMP280_IIO_PATH);
        fp = fopen(path, "r");
        if (fp) {
            if (fscanf(fp, "%d", &temp_raw) == 1) {
                shared_data->bmp_temp = temp_raw / 100.0f;
                shared_data->bmp_valid = 1;
            }
            fclose(fp);
        }

        snprintf(path, sizeof(path), "%s/in_pressure_raw", BMP280_IIO_PATH);
        fp = fopen(path, "r");
        if (fp) {
            if (fscanf(fp, "%d", &press_raw) == 1) {
                shared_data->bmp_press = press_raw;
            }
            fclose(fp);
        }

        usleep(200000);
    }
    return NULL;
}

// ============ 主函数 ============
int main(void) {
    pthread_t mpu_tid, gps_tid, max_tid, bmp_tid;
    time_t last_print_time = 0;
    int i = 0;

    // 创建真正的 POSIX 共享内存
    shm_fd = shm_open("/sensor_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("[错误] 无法创建共享内存！请检查权限。");
        exit(1);
    }
    
    // 设置共享内存大小
    if (ftruncate(shm_fd, sizeof(sensor_data_t)) == -1) {
        perror("[错误] 无法设置共享内存大小！");
        close(shm_fd);
        exit(1);
    }

    // 将共享内存映射到当前进程的内存空间
    shared_data = (sensor_data_t *)mmap(0, sizeof(sensor_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        perror("[错误] 共享内存映射失败！");
        close(shm_fd);
        exit(1);
    }

    // 初始化共享内存数据为零
    memset(shared_data, 0, sizeof(sensor_data_t));

    // ---------- 陀螺仪校准 ----------
    FILE *calib = fopen("/dev/mpu6500", "r");
    int cal_sample = 300;
    if (calib != NULL) {
        printf("开始陀螺仪零偏校准，请保持设备静止3秒...\n");
        char tmpbuf[128];
        int16_t ax, ay, az, gx, gy, gz;
        gx_offset = 0; gy_offset = 0; gz_offset = 0;
        for (i = 0; i < cal_sample; i++) {
            fgets(tmpbuf, sizeof(tmpbuf), calib);
            sscanf(tmpbuf, "ax:%hd ay:%hd az:%hd gx:%hd gy:%hd gz:%hd", &ax, &ay, &az, &gx, &gy, &gz);
            gx_offset += gx;
            gy_offset += gy;
            gz_offset += gz;
            usleep(10000);
        }
        fclose(calib);
        gx_offset /= cal_sample;
        gy_offset /= cal_sample;
        gz_offset /= cal_sample;
        printf("陀螺仪校准完成：GX=%.2f  GY=%.2f  GZ=%.2f LSB\n\n", gx_offset, gy_offset, gz_offset);
    }

    // ---------- 初始化卡尔曼 ----------
    kalman_init(&speed_kf, 0.008f, 0.25f);
    clock_gettime(CLOCK_MONOTONIC, &last_time);

    printf("===== 四传感器融合：IMU + GPS + 心率血氧 + 气压计 + 摔倒检测 =====\n");
    printf("启动线程...\n");

    pthread_create(&mpu_tid, NULL, mpu_thread_func, NULL);
    pthread_create(&gps_tid, NULL, gps_thread_func, NULL);
    pthread_create(&max_tid, NULL, max30102_thread_func, NULL);
    pthread_create(&bmp_tid, NULL, bmp280_thread_func, NULL);

    // ---------- 主循环打印 + GPS 定时上报 ----------
    static time_t last_gps_send_time = 0;

    while (1) {
        time_t now = time(NULL);
        shared_data->timestamp = now;

        if (now - last_print_time >= 1) {
            last_print_time = now;

            printf("\n[%s]", ctime(&now));
            printf("IMU: AX=%+5.2fg AY=%+5.2fg AZ=%+5.2fg | GX=%+5.1f GY=%+5.1f GZ=%+5.1f (°/s)\n",
                   shared_data->ax, shared_data->ay, shared_data->az,
                   shared_data->gx, shared_data->gy, shared_data->gz);
            printf("静止标志：%d | GPS零速帧：%d\n", device_is_still, gps_zero_speed_cnt);

            if (shared_data->gps_fix) {
                printf("GPS: 定位成功 | 原始速度=%.1f km/h | 滤波后=%.1f km/h\n",
                       shared_data->speed_kmh, shared_data->filtered_speed);
                printf("经度=%.6f 纬度=%.6f\n", shared_data->lon, shared_data->lat);
            } else {
                printf("GPS: 未定位，滤波速度=%.1f km/h\n", shared_data->filtered_speed);
            }

            if (shared_data->hr_valid) {
                printf("心率：%d bpm  ", shared_data->heart_rate);
            } else {
                printf("心率：--- bpm  ");
            }
            if (shared_data->spo2_valid) {
                printf("血氧：%d%%  ", shared_data->spo2);
            } else {
                printf("血氧：---%%  ");
            }
            printf("温度：%.2f℃\n", shared_data->chip_temp);

            if (shared_data->bmp_valid) {
                printf("气压：%.2f kPa  温度：%.1f℃\n", shared_data->bmp_press / 1000.0f, shared_data->bmp_temp);
            } else {
                printf("BMP280: 无效\n");
            }

            if (shared_data->fall_detected) {
                printf("⚠️⚠️⚠️ 检测到摔倒！⚠️⚠️⚠️\n");
            }
        }

        // ===== 每 5 秒发送一次 GPS 数据 =====
        if (now - last_gps_send_time >= 5) {
            if (shared_data->gps_fix) {
                publish_gps_data(shared_data->lat, shared_data->lon, shared_data->speed_kmh);
            }
            last_gps_send_time = now;
        }

        usleep(100000);
    }

    pthread_join(mpu_tid, NULL);
    pthread_join(gps_tid, NULL);
    pthread_join(max_tid, NULL);
    pthread_join(bmp_tid, NULL);

    if (shared_data != NULL) {
        munmap(shared_data, sizeof(sensor_data_t));
    }
    if (shm_fd != -1) {
        close(shm_fd);
    }
    
    return 0;
}