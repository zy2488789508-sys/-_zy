#!/bin/bash
DEST="/home/xyu/Linux/nfs/rootfs/lib/modules/4.1.15-ge48931b1/"
# 判断是否传入文件参数
if [ $# -eq 0 ];then
    echo "使用方式：$0 模块.ko 测试APP"
    echo "示例：$0 ap3216c.ko ap3216cAPP"
    exit 1
fi
# 执行拷贝
sudo cp "$@" "$DEST" -f
echo "✅ 本次已拷贝文件：$@"
echo "==== 校验目标目录内本次复制的文件 ===="
# 只过滤本次传入的文件展示
for file in "$@";do
    ls -lh "$DEST/$file"
done