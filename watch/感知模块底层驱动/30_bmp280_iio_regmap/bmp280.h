#ifndef __BMP280_H
#define __BMP280_H
/*==============================================================================
 * 1. BMP280 寄存器地址定义 (Register Addresses)
 * 
 *============================================================================*/
/*==============================================================================
 * 1. 校准数据寄存器 (Calibration Data) 0x88 ~ 0xA1
 *    共 26 个字节，包含温度补偿系数和压力补偿系数
 *============================================================================*/

/* 温度补偿系数 (3个u16) */
#define BMP280_REG_DIG_T1_LSB          0x88   /* 校准数据 T1 低字节 */
#define BMP280_REG_DIG_T1_MSB          0x89   /* 校准数据 T1 高字节 */
#define BMP280_REG_DIG_T2_LSB          0x8A   /* 校准数据 T2 低字节 */
#define BMP280_REG_DIG_T2_MSB          0x8B   /* 校准数据 T2 高字节 */
#define BMP280_REG_DIG_T3_LSB          0x8C   /* 校准数据 T3 低字节 */
#define BMP280_REG_DIG_T3_MSB          0x8D   /* 校准数据 T3 高字节 */

/* 压力补偿系数 (9个u16) */
#define BMP280_REG_DIG_P1_LSB          0x8E   /* 校准数据 P1 低字节 */
#define BMP280_REG_DIG_P1_MSB          0x8F   /* 校准数据 P1 高字节 */
#define BMP280_REG_DIG_P2_LSB          0x90   /* 校准数据 P2 低字节 */
#define BMP280_REG_DIG_P2_MSB          0x91   /* 校准数据 P2 高字节 */
#define BMP280_REG_DIG_P3_LSB          0x92   /* 校准数据 P3 低字节 */
#define BMP280_REG_DIG_P3_MSB          0x93   /* 校准数据 P3 高字节 */
#define BMP280_REG_DIG_P4_LSB          0x94   /* 校准数据 P4 低字节 */
#define BMP280_REG_DIG_P4_MSB          0x95   /* 校准数据 P4 高字节 */
#define BMP280_REG_DIG_P5_LSB          0x96   /* 校准数据 P5 低字节 */
#define BMP280_REG_DIG_P5_MSB          0x97   /* 校准数据 P5 高字节 */
#define BMP280_REG_DIG_P6_LSB          0x98   /* 校准数据 P6 低字节 */
#define BMP280_REG_DIG_P6_MSB          0x99   /* 校准数据 P6 高字节 */
#define BMP280_REG_DIG_P7_LSB          0x9A   /* 校准数据 P7 低字节 */
#define BMP280_REG_DIG_P7_MSB          0x9B   /* 校准数据 P7 高字节 */
#define BMP280_REG_DIG_P8_LSB          0x9C   /* 校准数据 P8 低字节 */
#define BMP280_REG_DIG_P8_MSB          0x9D   /* 校准数据 P8 高字节 */
#define BMP280_REG_DIG_P9_LSB          0x9E   /* 校准数据 P9 低字节 */
#define BMP280_REG_DIG_P9_MSB          0x9F   /* 校准数据 P9 高字节 */

/* 校准数据末尾（可能包含其他值，标准 BMP280 到 0x9F 结束） */
#define BMP280_REG_CALIB_END           0xA1   /* 校准数据结束地址（含保留字节） */

/* 为了方便，定义校准数据的起始和结束地址 */
#define BMP280_REG_CALIB_START         0x88
#define BMP280_CALIB_DATA_LEN          24     /* 从 0x88 到 0x9F 共 24 字节 */


/*==============================================================================
 * 2. 控制寄存器 (Control Registers)
 *============================================================================*/

#define BMP280_REG_ID                  0xD0   /* 芯片ID, 固定 0x58 */
#define BMP280_REG_RESET               0xE0   /* 软复位, 写入 0xB6 */
#define BMP280_REG_STATUS              0xF3   /* 状态寄存器 */
#define BMP280_REG_CTRL_MEAS           0xF4   /* 控制测量 */
#define BMP280_REG_CONFIG              0xF5   /* 配置寄存器 */


/*==============================================================================
 * 3. 数据寄存器 (Data Registers)
 *============================================================================*/

#define BMP280_REG_PRESS_MSB           0xF7   /* 压力数据 高字节 (bit[19:12]) */
#define BMP280_REG_PRESS_LSB           0xF8   /* 压力数据 中字节 (bit[11:4])  */
#define BMP280_REG_PRESS_XLSB          0xF9   /* 压力数据 低字节 (bit[3:0])   */

#define BMP280_REG_TEMP_MSB            0xFA   /* 温度数据 高字节 (bit[19:12]) */
#define BMP280_REG_TEMP_LSB            0xFB   /* 温度数据 中字节 (bit[11:4])  */
#define BMP280_REG_TEMP_XLSB           0xFC   /* 温度数据 低字节 (bit[3:0])   */


/*==============================================================================
 * 4. 位域定义 (Bit Masks & Shifts)
 *============================================================================*/

/* -------- 寄存器 0xF4 : ctrl_meas -------- */
#define BMP280_CTRL_MEAS_OSRS_T_MASK   0xE0   /* bit[7:5] 温度过采样 */
#define BMP280_CTRL_MEAS_OSRS_T_SHIFT  5
#define BMP280_CTRL_MEAS_OSRS_P_MASK   0x1C   /* bit[4:2] 压力过采样 */
#define BMP280_CTRL_MEAS_OSRS_P_SHIFT  2
#define BMP280_CTRL_MEAS_MODE_MASK     0x03   /* bit[1:0] 电源模式 */

/* -------- 寄存器 0xF5 : config -------- */
#define BMP280_CONFIG_T_SB_MASK        0xE0   /* bit[7:5] 待机时间 */
#define BMP280_CONFIG_T_SB_SHIFT       5
#define BMP280_CONFIG_FILTER_MASK      0x1C   /* bit[4:2] 滤波器系数 */
#define BMP280_CONFIG_FILTER_SHIFT     2
#define BMP280_CONFIG_SPI3W_EN_MASK    0x01   /* bit[0] SPI 3线使能 */

/* -------- 寄存器 0xF3 : status -------- */
#define BMP280_STATUS_MEASURING_MASK   0x08   /* bit[3] 转换进行中 */
#define BMP280_STATUS_IM_UPDATE_MASK   0x01   /* bit[0] NVM 更新中 */

/* -------- 寄存器 0xD0 : id -------- */
#define BMP280_ID_CHIP_ID              0x58   /* 固定值 */


/*==============================================================================
 * 5. 常用配置值 (Enums / Macros)
 *============================================================================*/

/* 5.1 电源模式 */
#define BMP280_MODE_SLEEP              0x00
#define BMP280_MODE_FORCED             0x01
#define BMP280_MODE_NORMAL             0x03

/* 5.2 温度/压力过采样 */
#define BMP280_OSRS_SKIPPED            0x00
#define BMP280_OSRS_X1                 0x01
#define BMP280_OSRS_X2                 0x02
#define BMP280_OSRS_X4                 0x03
#define BMP280_OSRS_X8                 0x04
#define BMP280_OSRS_X16                0x05

/* 5.3 待机时间 (仅 Normal 模式) */
#define BMP280_T_SB_0_5_MS             0x00
#define BMP280_T_SB_62_5_MS            0x01
#define BMP280_T_SB_125_MS             0x02
#define BMP280_T_SB_250_MS             0x03
#define BMP280_T_SB_500_MS             0x04
#define BMP280_T_SB_1000_MS            0x05
#define BMP280_T_SB_2000_MS            0x06
#define BMP280_T_SB_4000_MS            0x07

/* 5.4 滤波器系数 */
#define BMP280_FILTER_OFF              0x00
#define BMP280_FILTER_COEFF_2          0x01
#define BMP280_FILTER_COEFF_4          0x02
#define BMP280_FILTER_COEFF_8          0x03
#define BMP280_FILTER_COEFF_16         0x04

/* 5.5 软复位值 */
#define BMP280_RESET_VALUE             0xB6


#endif