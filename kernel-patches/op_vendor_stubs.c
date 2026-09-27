/*
 * OnePlusOSS 公开树缺失的厂商私有函数 —— 中性桩
 *
 * 这些函数在原厂内核里有一加私有实现（读 bootloader 共享内存 / param 分区 /
 * 前台任务识别），公开树未包含其源码。这里只保证链接完整与“正常开机”路径安全，
 * 不提供厂商增强：
 *   get_boot_mode            恒返回 NORMAL
 *   is_fg                    恒 false（关闭块层前台 IO 提权）
 *   init_param_mem_base_size 空实现（param 分区信息不使用）
 *   get_param_by_index_...   返回 -ENODATA，不修改调用方缓冲区
 *   set_param_by_index_...   返回 -ENOSYS（不写 param 分区）
 *   oem_check_force_dump_key 空实现（忽略工厂强制转储组合键）
 *   ht_register_thermal...   空实现（跳过厂商温控 hook 注册）
 *   btfm_slim_hw_init        返回 -ENODEV（BTFM_SLIM 被禁用，仅工厂测试 ioctl 触及）
 *   gf_opticalfp_irq_handler 返回 0（6T 屏下指纹接口，enchilada 无此硬件）
 */
#include <linux/types.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/oneplus/boot_mode.h>
#include <linux/param_rw.h>
#include <linux/thermal.h>
#include <linux/oem_force_dump.h>

enum oem_boot_mode get_boot_mode(void)
{
	return MSM_BOOT_MODE__NORMAL;
}

/* 声明位于 block/blk.h */
bool is_fg(int uid)
{
	return false;
}

/* 声明位于 drivers/of/fdt.c */
void init_param_mem_base_size(phys_addr_t base, unsigned long size)
{
}

int get_param_by_index_and_offset(uint32 sid_index, uint32 offset,
				  void *buf, int length)
{
	return -ENODATA;
}

int set_param_by_index_and_offset(uint32 sid_index, uint32 offset,
				  void *buf, int length)
{
	return -ENOSYS;
}

void oem_check_force_dump_key(unsigned int code, int value)
{
}

void ht_register_thermal_zone_device(struct thermal_zone_device *tz)
{
}

/* 前置声明即可，仅工厂测试 ioctl 会调用；正常蓝牙走 UART 电源路径 */
#ifndef CONFIG_BTFM_SLIM
struct btfmslim;
int btfm_slim_hw_init(struct btfmslim *btfmslim)
{
	return -ENODEV;
}
#endif

/*
 * 6T 屏下光学指纹（Goodix）中断通知，6/6T 共用触摸源码里无条件调用。
 * enchilada 无此硬件、相关手势运行时不会触发，返回 0 即可。
 */
int gf_opticalfp_irq_handler(int event)
{
	return 0;
}

/*
 * 另一套屏下指纹（silead/汇顶等，随触摸源码内联）的通知接口，
 * enchilada 无屏下指纹硬件，空实现即可。
 */
struct fp_underscreen_info;
int opticalfp_irq_handler(struct fp_underscreen_info *tp_info)
{
	return 0;
}
