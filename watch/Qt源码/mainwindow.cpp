#include "mainwindow.h"
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDateTime>
#include <QPainterPath>
#include <QDebug>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#define SHM_NAME "/sensor_shm"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , shm_fd(-1)
    , shared_data(nullptr)
    , timer(new QTimer(this))
    , data_count(0)
    , gps_first_point(true)
    , last_fall_state(false)
{
    // 1. 创建中心部件
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    setWindowTitle("传感器数据分析系统");

    // 2. 初始化底层数据模块
    initSharedMemory();
    initCharts();
    initGPSTracker();

    // 3. 创建 UI 控件
    timeLabel = new QLabel("时间: --:--:--");
    timeLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    accelLabel = new QLabel("加速度: X:0.00 Y:0.00 Z:0.00");
    accelLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    gyroLabel = new QLabel("陀螺仪: X:0.00 Y:0.00 Z:0.00");
    gyroLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    gpsInfoLabel = new QLabel("GPS: 未定位");
    gpsInfoLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    speedLabel = new QLabel("速度: 0.00 km/h");
    speedLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    hrLabel = new QLabel("心率: -- bpm");
    hrLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    spo2Label = new QLabel("血氧: -- %");
    spo2Label->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    tempLabel = new QLabel("温度: -- ℃");
    tempLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    bmpLabel = new QLabel("气压: -- hPa");
    bmpLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    stillLabel = new QLabel("静止状态: 未知");
    stillLabel->setStyleSheet("border: 1px solid #666666; padding: 5px;");

    hrProgressBar = new QProgressBar();

    // 4. 搭建第0页：主界面
    mainPage = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(mainPage);

    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(0);
    grid->setVerticalSpacing(0);
    grid->setContentsMargins(0, 0, 0, 0);

    grid->addWidget(timeLabel, 0, 0, 1, 2);
    grid->addWidget(accelLabel, 1, 0);
    grid->addWidget(gyroLabel, 1, 1);
    grid->addWidget(hrLabel, 2, 0);
    grid->addWidget(hrProgressBar, 2, 1);
    grid->addWidget(spo2Label, 3, 0);
    grid->addWidget(tempLabel, 3, 1);
    grid->addWidget(speedLabel, 4, 0);
    grid->addWidget(bmpLabel, 4, 1);
    grid->addWidget(gpsInfoLabel, 5, 0);
    grid->addWidget(stillLabel, 5, 1);
    mainLayout->addLayout(grid);

    viewGpsBtn = new QPushButton("点击查看完整 GPS 轨迹");
    viewGpsBtn->setFixedHeight(40);
    mainLayout->addWidget(viewGpsBtn);

    mainLayout->addWidget(chartView);

    // 5. GPS轨迹页面
    gpsPage = new QWidget();
    QVBoxLayout *gpsLayout = new QVBoxLayout(gpsPage);

    QHBoxLayout *gpsTopLayout = new QHBoxLayout();
    backBtn = new QPushButton("← 返回主界面");
    backBtn->setFixedSize(120, 40);
    gpsTopLayout->addWidget(backBtn);
    gpsTopLayout->addStretch();
    gpsTopLayout->addWidget(new QLabel("GPS 轨迹跟踪中..."));
    gpsLayout->addLayout(gpsTopLayout);

    gpsLabel = new QLabel("等待 GPS 定位...");
    gpsLabel->setAlignment(Qt::AlignCenter);
    gpsLabel->setStyleSheet("background-color: #f0f0f0; border: 1px solid #ccc;");
    gpsLayout->addWidget(gpsLabel);

    stackedWidget = new QStackedWidget(centralWidget);
    stackedWidget->addWidget(mainPage);
    stackedWidget->addWidget(gpsPage);
    stackedWidget->setCurrentIndex(0);

    QVBoxLayout *finalLayout = new QVBoxLayout(centralWidget);
    finalLayout->addWidget(stackedWidget);

    connect(viewGpsBtn, &QPushButton::clicked, this, [=](){
        stackedWidget->setCurrentIndex(1);
    });
    connect(backBtn, &QPushButton::clicked, this, [=](){
        stackedWidget->setCurrentIndex(0);
    });

    connect(timer, &QTimer::timeout, this, &MainWindow::updateData);
    timer->start(100);
}

MainWindow::~MainWindow()
{
    timer->stop();
    if (shared_data != nullptr) {
        munmap(shared_data, sizeof(sensor_data_t));
    }
    if (shm_fd != -1) {
        ::close(shm_fd);
    }
}

void MainWindow::initSharedMemory()
{
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        QMessageBox::warning(this, "错误", "共享内存打开失败，请先运行数据生成程序！");
        return;
    }
    shared_data = (sensor_data_t *)mmap(0, sizeof(sensor_data_t), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        QMessageBox::warning(this, "错误", "共享内存映射失败！");
        shared_data = nullptr;
        ::close(shm_fd);
        shm_fd = -1;
    }
}

void MainWindow::initCharts()
{
    QChart *chart = new QChart();
    chart->setTitle("实时传感器数据");
    chart->legend()->setVisible(true);

    hr_series = new QLineSeries();
    hr_series->setName("心率 (bpm)");
    hr_series->setPen(QPen(Qt::red, 3));

    speed_series = new QLineSeries();
    speed_series->setName("速度 (km/h)");
    speed_series->setPen(QPen(Qt::blue, 3));

    axisX = new QValueAxis();
    axisX->setTitleText("时间 (帧)");
    axisX->setRange(0, 100);
    axisX->setLabelFormat("%d");

    axisY_hr = new QValueAxis();
    axisY_hr->setTitleText("心率 (bpm)");
    axisY_hr->setRange(0, 200);

    axisY_speed = new QValueAxis();
    axisY_speed->setTitleText("速度 (km/h)");
    axisY_speed->setRange(-1, 50); //防止0数值被坐标轴遮挡

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY_hr, Qt::AlignLeft);
    chart->addAxis(axisY_speed, Qt::AlignRight);

    hr_series->attachAxis(axisX);
    hr_series->attachAxis(axisY_hr);
    speed_series->attachAxis(axisX);
    speed_series->attachAxis(axisY_speed);

    chart->addSeries(hr_series);
    chart->addSeries(speed_series);

    // 重新绑定
    hr_series->attachAxis(axisX);
    hr_series->attachAxis(axisY_hr);
    speed_series->attachAxis(axisX);
    speed_series->attachAxis(axisY_speed);

    chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(250);
}

void MainWindow::initGPSTracker()
{
    mapPixmap = QPixmap(400, 300);
    mapPixmap.fill(Qt::lightGray);
    gps_first_point = true;
    last_lat = 0.0f;
    last_lon = 0.0f;
}

void MainWindow::updateData()
{
    if (shared_data == nullptr) return;
    sensor_data_t *current_data = shared_data;
    if (current_data == nullptr) return;

    updateLabels(*current_data);
    updateCharts(*current_data);
    updateGPS(*current_data);
    checkFallAlarm(*current_data);
}

void MainWindow::updateLabels(const sensor_data_t &data)
{
    QDateTime dt = QDateTime::fromSecsSinceEpoch(data.timestamp);
    timeLabel->setText(QString("时间: %1").arg(dt.toString("hh:mm:ss.zzz")));

    accelLabel->setText(QString("加速度: X:%1 Y:%2 Z:%3")
                        .arg(data.ax, 0, 'f', 2).arg(data.ay, 0, 'f', 2).arg(data.az, 0, 'f', 2));
    gyroLabel->setText(QString("陀螺仪: X:%1 Y:%2 Z:%3")
                       .arg(data.gx, 0, 'f', 2).arg(data.gy, 0, 'f', 2).arg(data.gz, 0, 'f', 2));

    speedLabel->setText(QString("速度: %1 km/h").arg(data.speed_kmh, 0, 'f', 2));
    hrLabel->setText(QString("心率: %1 bpm").arg(data.heart_rate, 0, 'f', 0));
    spo2Label->setText(QString("血氧: %1 %").arg(data.spo2, 0, 'f', 1));
    tempLabel->setText(QString("温度: %1 ℃").arg(data.bmp_temp, 0, 'f', 1));
    bmpLabel->setText(QString("气压: %1 hPa").arg(data.bmp_press, 0, 'f', 0));

    hrProgressBar->setValue(data.heart_rate);
    hrProgressBar->setRange(0, 200);

    if (data.gps_fix) {
        gpsInfoLabel->setText(QString("GPS: 定位成功 (纬度:%1, 经度:%2)")
                              .arg(data.lat, 0, 'f', 6).arg(data.lon, 0, 'f', 6));
    } else {
        gpsInfoLabel->setText("GPS: 未定位");
    }
    stillLabel->setText(data.gps_fix ? "静止状态: 运动中" : "静止状态: 静止");
}

void MainWindow::updateCharts(const sensor_data_t &data)
{
    hr_series->append(data_count, data.heart_rate);
    speed_series->append(data_count, data.speed_kmh);

    qDebug() << "点序号:" << data_count
             << "心率值:" << data.heart_rate
             << "速度值:" << data.speed_kmh
             << "心率总点数:" << hr_series->count();

    data_count++;
    const int max_points = 100;

    if (data_count > max_points)
    {
        hr_series->remove(0);
        speed_series->remove(0);
        axisX->setRange(data_count - max_points, data_count);
    }
    else
    {
        axisX->setRange(0, max_points);
    }

    chartView->update();
}

void MainWindow::updateGPS(const sensor_data_t &data)
{
    if (stackedWidget->currentIndex() != 1) return;
    if (!data.gps_fix || data.lat < 0.1f || data.lon < 0.1f) return;

    QPainter painter(&mapPixmap);
    painter.setPen(QPen(Qt::red, 3));

    int x = static_cast<int>((data.lon + 180.0) / 360.0 * 400);
    int y = static_cast<int>((90.0 - data.lat) / 180.0 * 300);

    if (gps_first_point) {
        painter.drawEllipse(x - 2, y - 2, 4, 4);
        gps_first_point = false;
    } else {
        int last_x = static_cast<int>((last_lon + 180.0) / 360.0 * 400);
        int last_y = static_cast<int>((90.0 - last_lat) / 180.0 * 300);
        painter.drawLine(last_x, last_y, x, y);
    }

    last_lat = data.lat;
    last_lon = data.lon;
    gpsLabel->setPixmap(mapPixmap);
}

void MainWindow::checkFallAlarm(const sensor_data_t &data)
{
    bool current_fall = (data.fall_detected != 0);
    if (current_fall != last_fall_state) {
        if (current_fall) {
            QMessageBox::critical(this, "紧急报警", "检测到跌倒！请立即检查人员安全！");
        }
        last_fall_state = current_fall;
    }
}
