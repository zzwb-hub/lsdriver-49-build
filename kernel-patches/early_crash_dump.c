/*
 * early_crash_dump.c — LSD_DIAG
 *
 * 移植内核在 ramoops/pstore 驱动就绪之前就因 "Fatal exception in interrupt"
 * 崩溃，标准 pstore 为空。本 panic 通知器不依赖控制台/ramoops 驱动，在 panic
 * 重启前把寄存器现场和栈回溯按 persistent_ram console 记录格式，直接写进 ramoops
 * 的 console 区物理内存（经 arm64 线性映射）。重启回原厂槽位后，原厂 ramoops
 * 会把它当作普通 console-ramoops 暴露在 /sys/fs/pstore。
 *
 * 实测原厂参数：region phys 0xAC300000, size 4M；record 0x40000/console 0x40000/
 * ftrace 0x40000/pmsg 0x200000/devinfo 0x1000。dump 区 = 4M - 那些 = 0x17f000，
 * 故 console 区 phys = 0xAC47F000。仅诊断构建编入，定位后移除。
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/panic.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <asm/memory.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>
#include <asm/cacheflush.h>

#define LSD_REGION_PHYS		0xAC300000UL
#define LSD_CONSOLE_OFF		0x17f000UL
#define LSD_CONSOLE_PHYS	(LSD_REGION_PHYS + LSD_CONSOLE_OFF)
#define LSD_CONSOLE_CAP		0x40000UL	/* 含头 */

#define DBGC			0x43474244U

#define LSD_MAX_PC		56
#define LSD_TXT_SZ		(LSD_CONSOLE_CAP - 12)

struct pram {
	u32 sig;
	s32 start;
	s32 size;
	u8 data[0];
};

static long lsd_seen;

void lsd_capture(struct pt_regs *regs);

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

static int lsd_panic(struct notifier_block *nb, unsigned long ev, void *v)
{
	/* die() 走 panic 时不传 pt_regs；真正的现场由 traps.c 在 panic 前调用
	 * lsd_capture(regs) 已写入，这里仅兜底处理无现场的其它 panic */
	lsd_capture((struct pt_regs *)v);
	return NOTIFY_OK;
}

void lsd_capture(struct pt_regs *regs)
{
	struct pram *pr;
	char *t;
	char *p;
	int i;
	struct stackframe sf;
	u64 frames[LSD_MAX_PC];
	int nframes = 0;
	u64 prev = 0;

	if (cmpxchg(&lsd_seen, 0, 1))
		return;

	if (regs && nframes < LSD_MAX_PC)
		frames[nframes++] = regs->pc;
	memset(&sf, 0, sizeof(sf));
	sf.fp = regs ? regs->regs[29] : (u64)(u64 *)__builtin_frame_address(0);
	sf.sp = regs ? regs->sp : 0;
	sf.pc = regs ? regs->pc : (u64)lsd_panic;
	while (nframes < LSD_MAX_PC && sf.fp && sf.pc) {
		if (unwind_frame(current, &sf) < 0)
			break;
		if (!sf.pc || sf.pc == prev)
			break;
		prev = sf.pc;
		frames[nframes++] = sf.pc;
	}

	/* 崩溃在中断已使能之后，arm64 线性映射已建立 */
	pr = (struct pram *)__phys_to_virt(LSD_CONSOLE_PHYS);
	t = (char *)pr->data;
	p = t;

	p += sprintf(p, "====== LSD EARLY CRASH ======\n");
	p += sprintf(p, "cpu=%u irq=%u pid=%ld comm=%s\n",
		     smp_processor_id(), in_interrupt() ? 1 : 0,
		     (long)current->pid, current->comm);
	if (regs) {
		p += sprintf(p, "pc="); p = lx64(p, regs->pc); *p++ = '\n';
		p += sprintf(p, "lr="); p = lx64(p, regs->regs[30]); *p++ = '\n';
		p += sprintf(p, "sp="); p = lx64(p, regs->sp); *p++ = '\n';
		p += sprintf(p, "pstate="); p = lx64(p, regs->pstate); *p++ = '\n';
	}
	p += sprintf(p, "--- backtrace ---\n");
	for (i = 0; i < nframes; i++) {
		p += sprintf(p, "[%2d] ", i);
		p = lx64(p, frames[i]);
		*p++ = '\n';
		if (p > t + LSD_TXT_SZ - 90)
			break;
	}
	p += sprintf(p, "====== END LSD ======\n");

	pr->start = 0;
	pr->size = (s32)(p - t);
	/* sig 最后写：原厂 post_init 以 sig 判定有效 */
	wmb();
	pr->sig = DBGC;
	__flush_dcache_area(pr, LSD_CONSOLE_CAP);
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
