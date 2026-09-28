/*
 * early_crash_dump.c — LSD_DIAG
 *
 * 移植内核在 ramoops/pstore 驱动就绪之前就因 "Fatal exception in interrupt"
 * 崩溃。本文件在 panic 之前（寄存器现场尚在）把现场和原始栈内存按 persistent_ram
 * console 记录格式直接写进 ramoops 的 console 区物理内存（经 arm64 线性映射）。
 *
 * 恢复链：崩溃后让 restart 走 "bootloader" 路径（清 dload magic + 热复位 +
 * restart reason 0x77665500），手机自动进 Fastboot 菜单且 RAM 不断电；用户在
 * fastboot 切 b 槽正常重启，原厂 ramoops 即把本记录当作 console-ramoops 暴露。
 *
 * 实测原厂参数：region phys 0xAC300000；dump records 占 5×0x40000=0x140000，
 * 故 console 区 phys = 0xAC440000。IMEM phys 0x146bf000，restart reason @0x65c。
 * 仅诊断构建编入，定位后移除。
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <asm/memory.h>
#include <asm/ptrace.h>
#include <asm/cacheflush.h>
#include <linux/lsd_bootstage.h>

#define LSD_CONSOLE_PHYS	0xAC440000UL
#define LSD_CONSOLE_CAP		0x40000UL	/* 含12字节头 */
#define LSD_KASLR_OFF_PHYS	0x146bf6d0UL	/* imem kaslr_offset 首字 */

#define DBGC			0x43474244U

#define LSD_TXT_SZ		(LSD_CONSOLE_CAP - 12)
#define LSD_STACK_DUMP		2048

struct pram {
	u32 sig;
	s32 start;
	s32 size;
	u8 data[0];
};

static long lsd_seen;

/* msm-poweroff.c 的 restart prepare 读取：非0时改走 bootloader 热复位路径 */
int lsd_diag_fastboot;

/* lsd_death_check 在主动 panic 前置位：本次 console 记录因此附带"上次死亡阶段" */
int lsd_prev_stage;

/*
 * 开机第一条 C 代码（stage20 之前）调用。读"墓穴"：它由本次启动的汇编
 * stage10 从 IMEM 搬入，保存的是上一次启动的终止阶段。若早于 GOOD，
 * 判定为静默卡死：置位 prev_stage、消费掉墓穴后主动 panic；panic 通知器
 * (本文件)把 prev_stage 写进 ramoops console，再热复位进 fastboot。
 */
void lsd_death_check(void)
{
	u8 *b = (u8 *)__phys_to_virt(LSD_BS_IMEM_PHYS);
	u32 magic, stage;

	magic = *(u32 *)(b + LSD_BS_GRAVE_MAGIC_OFF);
	stage = *(u32 *)(b + LSD_BS_GRAVE_STAGE_OFF);

	if (magic == LSD_BS_GRAVE_MAGIC_VAL && stage && stage < LSD_BS_STAGE_GOOD) {
		lsd_prev_stage = (int)stage;
		*(u32 *)(b + LSD_BS_GRAVE_MAGIC_OFF) = LSD_BS_MAGIC_DONE;
		wmb();
		panic("LSD: previous boot died at boot stage %u", stage);
	}
}

/* ---- 无条件探针（临时）：跑到第一条C代码即 dump IMEM 并 panic ---- */
int lsd_probe_active;
u32 lsd_pb[6];

void lsd_probe(void)
{
	u8 *b = (u8 *)__phys_to_virt(LSD_BS_IMEM_PHYS);

	lsd_pb[0] = *(u32 *)(b + 0x700);	/* cur magic */
	lsd_pb[1] = *(u32 *)(b + 0x708);	/* cur stage */
	lsd_pb[2] = *(u32 *)(b + 0x70c);	/* grave magic */
	lsd_pb[3] = *(u32 *)(b + 0x710);	/* grave stage */
	lsd_pb[4] = *(u32 *)(b + 0x65c);	/* restart reason */
	lsd_pb[5] = *(u32 *)(b + 0x6d0);	/* kaslr */
	lsd_probe_active = 1;
	panic("LSD PROBE: reached first C code (see imem dump)");
}

static char *lx64(char *p, u64 v)
{
	int i;
	*p++ = '0'; *p++ = 'x';
	for (i = 60; i >= 0; i -= 4) {
		u8 d = (u8)((v >> i) & 0xf);
		*p++ = d < 10 ? (char)('0' + d) : (char)('a' + d - 10);
	}
	return p;
}

void lsd_capture(struct pt_regs *regs)
{
	struct pram *pr;
	char *t;
	char *p;
	u64 sp;
	int i;

	if (cmpxchg(&lsd_seen, 0, 1))
		return;

	/* 通知 msm_restart_prepare 改走热复位→fastboot */
	lsd_diag_fastboot = 1;

	/* 中断已使能说明线性映射已建立；0xAC440000 在保留内存内、映射有效 */
	pr = (struct pram *)__phys_to_virt(LSD_CONSOLE_PHYS);
	t = (char *)pr->data;
	p = t;

	p += sprintf(p, "====== LSD EARLY CRASH ======\n");
	p += sprintf(p, "cpu=%u irq=%u pid=%ld comm=%s\n",
		     smp_processor_id(), in_interrupt() ? 1 : 0,
		     (long)current->pid, current->comm);
	if (lsd_prev_stage)
		p += sprintf(p, "prev_death_stage=%d\n", lsd_prev_stage);
	if (lsd_probe_active) {
		p += sprintf(p, "IMEM cur_magic=0x%08x cur_stage=%u\n", lsd_pb[0], lsd_pb[1]);
		p += sprintf(p, "IMEM grave_magic=0x%08x grave_stage=%u\n", lsd_pb[2], lsd_pb[3]);
		p += sprintf(p, "IMEM restart_reason=0x%08x kaslr=0x%08x\n", lsd_pb[4], lsd_pb[5]);
	}
	if (regs) {
		p += sprintf(p, "pc="); p = lx64(p, regs->pc); *p++ = '\n';
		p += sprintf(p, "lr="); p = lx64(p, regs->regs[30]); *p++ = '\n';
		sp = regs->sp;
		p += sprintf(p, "sp="); p = lx64(p, sp); *p++ = '\n';
		p += sprintf(p, "pstate="); p = lx64(p, regs->pstate); *p++ = '\n';
	} else {
		sp = (u64)(u64 *)__builtin_frame_address(0);
	}

	/* KASLR 偏移（IMEM 中内核写入），便于主机端符号化 */
	{
		u32 *ko = (u32 *)__phys_to_virt(LSD_KASLR_OFF_PHYS);
		p += sprintf(p, "kaslr_offset=0x%08x\n", *ko);
	}

	/* 原始栈内存逐字转储：不调用任何可能自崩的 unwinder；
	 * 主机端从这些数值里挑内核地址、按 System.map 符号化 */
	p += sprintf(p, "--- raw stack from sp (%d bytes) ---\n", LSD_STACK_DUMP);
	{
		u64 *s = (u64 *)sp;
		for (i = 0; i < LSD_STACK_DUMP / 8; i++) {
			if ((i & 3) == 0)
				p += sprintf(p, "%04x:", i * 8);
			p += sprintf(p, " %016llx", s[i]);
			if ((i & 3) == 3)
				*p++ = '\n';
			if (p > t + LSD_TXT_SZ - 40)
				goto done;
		}
	}
done:
	if (p[-1] != '\n')
		*p++ = '\n';
	p += sprintf(p, "====== END LSD ======\n");

	pr->start = 0;
	pr->size = (s32)(p - t);
	wmb();
	pr->sig = DBGC;			/* sig 最后写，原厂据此判定有效旧记录 */
	__flush_dcache_area(pr, LSD_CONSOLE_CAP);
}

/* 兜底：非 die() 路径的 panic（通知器收到的 v 可能为 pt_regs，也可能为 NULL） */
static int lsd_panic(struct notifier_block *nb, unsigned long ev, void *v)
{
	lsd_capture((struct pt_regs *)v);
	return NOTIFY_OK;
}

static struct notifier_block lsd_nb = {
	.notifier_call = lsd_panic,
	.priority = INT_MAX,
};

static int __init lsd_early_init(void)
{
	return atomic_notifier_chain_register(&panic_notifier_list, &lsd_nb);
}
pure_initcall(lsd_early_init);
