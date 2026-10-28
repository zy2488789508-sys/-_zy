#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/input.h>

#define PROXIMITY_THRESHOLD 20
#define SCREEN_ON_DURATION 10
#define CAPTURE_INTERVAL 1000000   /* 抓拍冷却时间1秒，防止连续触发 */

static int timer_count = 0;
static pid_t gst_preview_pid = -1;
static int capture_busy = 0;        /* 抓拍忙锁，防止重入 */

void screen_on() {
    int fb = open("/sys/class/graphics/fb0/blank", O_WRONLY);
    if (fb >= 0) { write(fb, "0", 1); close(fb); }
}

void screen_off() {
    int fb = open("/sys/class/graphics/fb0/blank", O_WRONLY);
    if (fb >= 0) { write(fb, "1", 1); close(fb); }
}

/* 启动预览 */
void start_preview(void)
{
    if(gst_preview_pid > 0){
        kill(gst_preview_pid, SIGKILL);
        waitpid(gst_preview_pid,NULL,0);
        gst_preview_pid = -1;
    }
    usleep(200000);  //等待内核释放/dev/video0
    gst_preview_pid = fork();
    if(gst_preview_pid == 0)
    {
        execlp("gst-launch-1.0",
            "gst-launch-1.0", "-q",
            "imxv4l2src","device=/dev/video0",
            "!","video/x-raw,width=640,height=480",
            "!","videoconvert",
            "!","fbdevsink","device=/dev/fb0",
            (char*)NULL);
        _exit(1);
    }
}

/* 抓拍，完成后恢复预览 */
void capture_and_restart(void)
{
    if(capture_busy) {
        printf("抓拍正在进行，跳过本次触发\n");
        return;
    }
    capture_busy = 1;

    //杀掉预览释放摄像头
    if(gst_preview_pid>0){
        kill(gst_preview_pid,SIGKILL);
        waitpid(gst_preview_pid,NULL,0);
        gst_preview_pid = -1;
    }
    usleep(300000); //重点！等待内核把/dev/video0彻底释放

    pid_t cap_pid = fork();
    if(cap_pid == 0){
        /* 输出保存到NFS共享目录 /opt/ui/cap.jpg，增加分辨率800*480 */
        execlp("gst-launch-1.0",
            "gst-launch-1.0","-q",
            "imxv4l2src","device=/dev/video0","num-buffers=1",
            "!","video/x-raw,width=800,height=480",
            "!","videoconvert",
            "!","jpegenc","quality=70",
            "!","filesink","location=/opt/ui/cap.jpg","sync=false",
            (char*)NULL);
        _exit(1);
    }
    //等待抓拍子进程结束
    waitpid(cap_pid,NULL,0);
    printf("抓拍子进程执行完毕\n");

    //重新启动预览
    start_preview();
    usleep(CAPTURE_INTERVAL);
    capture_busy = 0;
}

int main() {
    system("pkill -9 gst-launch-1.0 2>/dev/null");
    usleep(300000);

    int key_fd = open("/dev/input/event2", O_RDONLY);
    if (key_fd < 0) {
        printf("打开按键失败\n");
        return -1;
    }
    int flags = fcntl(key_fd, F_GETFL);
    fcntl(key_fd, F_SETFL, flags | O_NONBLOCK);

    int ps_fd = open("/sys/class/misc/ap3216c/ps", O_RDONLY);
    if (ps_fd < 0) {
        printf("警告：接近传感器打开失败\n");
    }

    printf("智能猫眼：启动gst预览常驻，默认黑屏待机...\n");
    start_preview();
    usleep(800000);
    screen_off();

    struct input_event ev;
    static int last_ps_state = 0;

    while (1) {
        //门铃按键
        if (read(key_fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1 && ev.code == 114) {
                printf("[触发]门铃按下，抓拍图片\n");
                timer_count = SCREEN_ON_DURATION*2;
                capture_and_restart();
            }
        }

        //AP3216C接近传感器
        if(ps_fd>0){
            char buf[16]={0};
            lseek(ps_fd,0,SEEK_SET);
            if(read(ps_fd,buf,sizeof(buf))>0){
                int ps_val = atoi(buf);
                int current_ps_state = (ps_val > PROXIMITY_THRESHOLD);
                if(current_ps_state && !last_ps_state){
                    if(timer_count == 0){
                        printf("[触发]检测到人靠近 PS=%d，抓拍图片\n",ps_val);
                        capture_and_restart();
                    }
                    timer_count = SCREEN_ON_DURATION*2;
                }
                last_ps_state = current_ps_state;
            }
        }

        if(timer_count>0){
            screen_on();
            timer_count--;
            usleep(500000);
        }else{
            screen_off();
            usleep(200000);
        }
    }

    if(gst_preview_pid>0){
        kill(gst_preview_pid,SIGKILL);
        waitpid(gst_preview_pid,NULL,0);
    }
    close(key_fd);
    if(ps_fd>0) close(ps_fd);
    return 0;
}