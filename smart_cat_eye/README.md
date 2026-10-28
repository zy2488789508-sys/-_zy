# 智能猫眼应用（smart_cat_eye）

基于 i.MX6ULL 的智能猫眼上层应用：通过 AP3216C 接近传感器与门铃按键触发，自动点亮屏幕、抓拍图片并恢复实时预览。

## 功能

- **常驻预览**：启动后 fork `gst-launch-1.0`（imxv4l2src + fbdevsink），摄像头画面直接输出到 LCD
- **低功耗待机**：无操作时通过 `/sys/class/graphics/fb0/blank` 关闭背光，降低功耗
- **接近唤醒**：轮询 AP3216C 的 PS 值（`/sys/class/misc/ap3216c/ps`），超过阈值即点亮屏幕并抓拍
- **门铃按键**：读取 `/dev/input/event2` 的按键事件（KEY_ENTER，code 114），按下即抓拍
- **抓拍防重入**：抓拍期间置忙锁，并在抓拍前后按 200~300 ms 等待 `/dev/video0` 释放，避免设备占用冲突
- **图片落地**：抓拍图片写入 `/opt/ui/cap.jpg`（NFS 共享目录，供上位机人脸识别读取）

## 依赖

| 依赖 | 说明 |
|---|---|
| `gst-launch-1.0` | 板端 GStreamer，含 `imxv4l2src`、`fbdevsink`、`jpegenc` 插件 |
| ap3216c 驱动 | 需先加载，提供 `/sys/class/misc/ap3216c/ps` 节点 |
| 按键设备 | `/dev/input/event2`（按实际板子枚举调整） |
| NFS | 抓拍目录 `/opt/ui/` 挂载到上位机 |

## 编译

```bash
# 交叉编译（i.MX6ULL，ARM 32 位）
arm-linux-gnueabihf-gcc smart_cat_eye.c -o smart_cat_eye
```

## 运行

```bash
# 板端
./smart_cat_eye
```

启动日志会打印预览启动、接近触发、抓拍完成等信息；`Ctrl+C` 结束。

## 辅助脚本

`cp.sh` 用于把驱动模块或应用程序一键拷贝到板子的 NFS rootfs 目录：

```bash
./cp.sh ap3216c.ko smart_cat_eye
```

> 脚本里的目标路径是作者本机环境，使用前请改成自己的 NFS rootfs 路径。

## 说明

仓库中不含编译产物 `smart_cat_eye`（ELF 可执行文件），请按上面的命令自行编译。