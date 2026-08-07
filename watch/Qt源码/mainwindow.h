#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>

#include "shared_data.h"

QT_CHARTS_USE_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateData();

private:
    // 共享内存
    int shm_fd;
    sensor_data_t *shared_data;

    // 定时器
    QTimer *timer;

    // 控件
    QLabel *timeLabel;
    QLabel *accelLabel;
    QLabel *gyroLabel;
    QLabel *gpsInfoLabel;
    QLabel *speedLabel;
    QLabel *hrLabel;
    QLabel *spo2Label;
    QLabel *tempLabel;
    QLabel *bmpLabel;
    QLabel *stillLabel;
    QProgressBar *hrProgressBar;
    QLabel *gpsLabel;

    // 图表
    QChartView *chartView;
    QLineSeries *hr_series;
    QLineSeries *speed_series;
    QValueAxis *axisX;
    QValueAxis *axisY_hr;
    QValueAxis *axisY_speed;
    int data_count;

    // GPS 轨迹
    QPixmap mapPixmap;
    bool gps_first_point;
    float last_lat, last_lon;

    // 报警状态
    bool last_fall_state;

    // 页面切换
    QStackedWidget *stackedWidget;
    QWidget *mainPage;
    QWidget *gpsPage;
    QPushButton *viewGpsBtn;
    QPushButton *backBtn;
    QPushButton *exitAppBtn;

    // 内部函数
    void initSharedMemory();
    void initCharts();
    void initGPSTracker();
    void updateLabels(const sensor_data_t &data);
    void updateCharts(const sensor_data_t &data);
    void updateGPS(const sensor_data_t &data);
    void checkFallAlarm(const sensor_data_t &data);
};

#endif // MAINWINDOW_H
