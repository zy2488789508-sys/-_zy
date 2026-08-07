#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xfa985410, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0xab5c8cb5, __VMLINUX_SYMBOL_STR(i2c_del_driver) },
	{ 0x19bdcb34, __VMLINUX_SYMBOL_STR(i2c_register_driver) },
	{ 0x4ee6e196, __VMLINUX_SYMBOL_STR(misc_register) },
	{ 0xf9a482f9, __VMLINUX_SYMBOL_STR(msleep) },
	{ 0x12b8611, __VMLINUX_SYMBOL_STR(regmap_read) },
	{ 0xfa2bcf10, __VMLINUX_SYMBOL_STR(init_timer_key) },
	{ 0x275ef902, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0xf62879ed, __VMLINUX_SYMBOL_STR(devm_regmap_init_i2c) },
	{ 0x93e3940, __VMLINUX_SYMBOL_STR(devm_kmalloc) },
	{ 0x40a6d9af, __VMLINUX_SYMBOL_STR(dev_err) },
	{ 0xd85cd67e, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0x6dc6de6d, __VMLINUX_SYMBOL_STR(regmap_bulk_read) },
	{ 0xb2d48a2e, __VMLINUX_SYMBOL_STR(queue_work_on) },
	{ 0x2d3385d3, __VMLINUX_SYMBOL_STR(system_wq) },
	{ 0x67c2fa54, __VMLINUX_SYMBOL_STR(__copy_to_user) },
	{ 0x1cfb04fa, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0x344b7739, __VMLINUX_SYMBOL_STR(prepare_to_wait_event) },
	{ 0x1000e51, __VMLINUX_SYMBOL_STR(schedule) },
	{ 0xb81960ca, __VMLINUX_SYMBOL_STR(snprintf) },
	{ 0x51d559d1, __VMLINUX_SYMBOL_STR(_raw_spin_unlock_irqrestore) },
	{ 0x598542b2, __VMLINUX_SYMBOL_STR(_raw_spin_lock_irqsave) },
	{ 0xc8fd727e, __VMLINUX_SYMBOL_STR(mod_timer) },
	{ 0x7d11c268, __VMLINUX_SYMBOL_STR(jiffies) },
	{ 0x3bd1b1f6, __VMLINUX_SYMBOL_STR(msecs_to_jiffies) },
	{ 0x6f0036d9, __VMLINUX_SYMBOL_STR(del_timer_sync) },
	{ 0xefd6cf06, __VMLINUX_SYMBOL_STR(__aeabi_unwind_cpp_pr0) },
	{ 0xcc839474, __VMLINUX_SYMBOL_STR(_dev_info) },
	{ 0xd9285795, __VMLINUX_SYMBOL_STR(misc_deregister) },
	{ 0xfef3f0ab, __VMLINUX_SYMBOL_STR(regmap_write) },
	{ 0x92b57248, __VMLINUX_SYMBOL_STR(flush_work) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("of:N*T*Cmy,mpu9250*");
MODULE_ALIAS("i2c:my,mpu9250");

MODULE_INFO(srcversion, "C9FFD25DFDA7A7DF64C6B6C");
