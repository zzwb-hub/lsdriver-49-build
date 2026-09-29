/*
 * OnePlusOSS Q_10.0 公开树缺失、且 P_9.0(Pie) 分支同样不提供的厂商私有函数 —— 中性桩。
 *
 * 说明：drivers/oneplus、drivers/oem_debug、drivers/param_read_write 已从 Pie 分支
 * 移植回真实现（见 kernel-patches/oneplus-graft 与 workflow 的
 * "Graft OnePlus vendor drivers (taken from Pie branch)" 步骤），因此原先放在
 * 这里的 get_boot_mode / init_param_mem_base_size / get_param_by_index_and_offset /
 * set_param_by_index_and_offset / oem_check_force_dump_key 桩**已删除**，
 * 以免与真实现符号重复造成链接错误。
 *
 * 本文件只保留 Pie 树也没有、但 Q 树内核里被无保护调用的极少数符号：
 *   is_fg                    Q 时代加进 block 层的接口（Q 公开树无实现）
 *   btfm_slim_hw_init        BTFM_SLIM 已被禁用，仅工厂测试 ioctl 触及
 *   ht_register_thermal_zone_device   HOUSTON 未启用（Q 公开树无实现）
 *   gf_opticalfp_irq_handler / opticalfp_irq_handler  屏下指纹通知（enchilada 无此硬件）
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/thermal.h>

/* 声明位于 block/blk.h —— 恒 false（关闭块层前台 IO 提权） */
bool is_fg(int uid)
{
	return false;
}

/* 前置声明即可，仅工厂测试 ioctl 会调用；正常蓝牙走 UART 电源路径 */
#ifndef CONFIG_BTFM_SLIM
struct btfmslim;
int btfm_slim_hw_init(struct btfmslim *btfmslim)
{
	return -ENODEV;
}
#endif

/* HOUSTON 温控 hook 注册：未启用该功能时空实现 */
void ht_register_thermal_zone_device(struct thermal_zone_device *tz)
{
}

/*
 * 6T 屏下光学指纹（Goodix）中断通知，6/6T 共用触摸源码里无条件调用。
 * enchilada 无此硬件、相关手势运行时不会触发，返回 0 即可。
 */
int gf_opticalfp_irq_handler(int event)
{
	return 0;
}

/*
 * 另一套屏下指纹（silead/汇顶等，随触摸源码内联）的通知接口。
 */
struct fp_underscreen_info;
int opticalfp_irq_handler(struct fp_underscreen_info *tp_info)
{
	return 0;
}