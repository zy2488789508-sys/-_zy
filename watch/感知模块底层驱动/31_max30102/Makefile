export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabi-
KERNELDIR :=/home/xyu/Linux/IMX6ULL/linux/linux-imx-4.1.15-2.1.0-e48931b1-v2.8
CURRENT_PATH :=$(shell pwd)

obj-m := max30102.o

buile: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules
clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean