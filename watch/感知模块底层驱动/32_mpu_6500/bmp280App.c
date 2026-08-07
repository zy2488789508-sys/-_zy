#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int fd;
    char *filename;
    char buf[64];
    int ret;

    if (argc != 2) {
        printf("Usage: %s <device_file>\n", argv[0]);
        return -1;
    }

    filename = argv[1];
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    while (1) {
        /* 读取数据（驱动返回的是字符串） */
        ret = read(fd, buf, sizeof(buf) - 1);//留出一个字节的空位防止越界
        if (ret > 0) {
            buf[ret] = '\0';   // 添加字符串结束符
            printf("%s", buf); // 打印，buf里已经有换行符了
        } else if (ret == 0) {
            printf("No data read\n");
        } else {
            perror("read");
            break;
        }
        usleep(100000); /* 100ms 轮询间隔 */
    }

    close(fd);
    return 0;
}