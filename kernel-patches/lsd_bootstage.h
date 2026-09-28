/* SPDX-License-Identifier: GPL-2.0 */
/*
 * lsd_bootstage.h - LSD_DIAG
 *
 * 在内核启动各阶段往 IMEM 写一个"进度编号"。IMEM 属 Always-On 域，
 * 冷启动(长按电源)后内容仍在，因此内核静默卡死(hang, 无panic/无USB)时，
 * 死亡位置会留在 IMEM，事后用 imem_reader 模块在正常系统里读出。
 *
 * IMEM phys 0x146bf000 (见 sdm845.dtsi)，0x700/0x708 为空闲偏移。
 * 仅诊断构建使用，定位后移除。
 */
#ifndef _LSD_BOOTSTAGE_H
#define _LSD_BOOTSTAGE_H

#include <linux/types.h>
#include <asm/memory.h>

#define LSD_BS_IMEM_PHYS	0x146bf000UL
#define LSD_BS_MAGIC_OFF	0x700
#define LSD_BS_STAGE_OFF	0x708
#define LSD_BS_GRAVE_MAGIC_OFF	0x70c
#define LSD_BS_GRAVE_STAGE_OFF	0x710
#define LSD_BS_MAGIC_VAL	0xB007B007U
#define LSD_BS_GRAVE_MAGIC_VAL	0x47524156U	/* "GRAV" */
#define LSD_BS_MAGIC_DONE	0x444F4E45U	/* "DONE"，已消费 */
#define LSD_BS_STAGE_GOOD	40		/* 跑到此阶段视为正常启动 */

static inline void lsd_boot_stage(u32 code)
{
	u8 *b = (u8 *)__phys_to_virt(LSD_BS_IMEM_PHYS);

	*(u32 *)(b + LSD_BS_MAGIC_OFF) = LSD_BS_MAGIC_VAL;
	*(u32 *)(b + LSD_BS_STAGE_OFF) = code;
	wmb();
}

/* 开机第一条C代码（stage20 之前）调用：若墓穴存有上次启动早于 GOOD 的
 * 终止阶段，说明上次静默卡死，则立刻 panic（经热复位把死亡阶段写进 pstore）。
 * 定义在 early_crash_dump.c。 */
void lsd_death_check(void);

/* 内核走到 exec init 前调用：取消启动进度看门狗。定义在 early_crash_dump.c */
void lsd_boot_ok(void);

/* 临时无条件探针：跑到第一条C即 dump IMEM 并 panic。定义在 early_crash_dump.c */
void lsd_probe(void);

#endif /* _LSD_BOOTSTAGE_H */
