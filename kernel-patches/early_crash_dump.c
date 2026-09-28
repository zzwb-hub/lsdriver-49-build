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
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/mm.h>
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

/* ---- boot progress deadline（抓"进度停滞型"静默卡死）----
 * early_initcall 时布 20s 定时器；内核走到 exec init 前调 lsd_boot_ok 取消。
 * 若在驱动初始化阶段挂起（关键线程睡眠/死亡、其余CPU在idle，定时器仍能跑），
 * 到期即在软中断里 panic，走已验证的热复位(保RAM、保pstore)。
 * 注意：不抓"关中断死循环"（那种定时器也不跑），那类留给硬件狗。 */
#define LSD_BOOT_DEADLINE_MS	20000
static struct timer_list lsd_boot_deadline;
static int lsd_boot_reached_userspace;

/* 前向声明: 定义在文件后部 param 写入器 */
static void lsd_param_write_now(void);
#define LSD_PARAM_SECTOR	16	/* offset 0x2000, 避开 BCB */
#define LSD_PARAM_MAGIC		0x4C534450U	/* "LSDP" */
struct lsd_param_rec;
static void lsd_fill_record(struct lsd_param_rec *rec);

static void lsd_boot_deadline_fn(unsigned long data)
{
	if (!lsd_boot_reached_userspace) {
		/* 紧急写入: 定时器软中断里一定能跑, 即使 workqueue 全卡死 */
		lsd_param_write_now();
		panic("LSD: boot deadline %dms exceeded - progress stall during init",
		      LSD_BOOT_DEADLINE_MS);
	}
}

void lsd_boot_ok(void)
{
	lsd_boot_reached_userspace = 1;
	smp_mb();
	del_timer_sync(&lsd_boot_deadline);

	/* 进用户空间前紧急同步写 param: 此时 UFS 必就绪, 进程上下文可睡眠。
	 * 用同步 bio 确保数据落盘后再继续, 防止系统后续异常重启丢失数据。 */
	{
		struct block_device *bdev;
		struct bio *bio;
		struct page *pg;
		struct lsd_param_rec *rec;

		bdev = blkdev_get_by_path("/dev/block/sda4",
					  FMODE_WRITE | FMODE_READ, NULL);
		if (IS_ERR(bdev))
			bdev = blkdev_get_by_dev(MKDEV(8, 4),
						FMODE_WRITE | FMODE_READ, NULL);
		if (!IS_ERR(bdev)) {
			pg = alloc_page(GFP_KERNEL);
			if (pg) {
				rec = (struct lsd_param_rec *)page_address(pg);
				lsd_fill_record(rec);
				__flush_dcache_area(rec, 4096);
				bio = bio_alloc(GFP_KERNEL, 1);
				if (bio) {
					bio->bi_bdev = bdev;
					bio->bi_opf = REQ_OP_WRITE;
					bio->bi_iter.bi_sector = LSD_PARAM_SECTOR;
					if (bio_add_page(bio, pg, 4096, 0) == 4096) {
						submit_bio_wait(bio);
						pr_info("LSD: param written synchronously at boot_ok\n");
					}
					bio_put(bio);
				}
				__free_page(pg);
			}
			blkdev_put(bdev, FMODE_WRITE | FMODE_READ);
		}
	}
}

static int __init lsd_arm_boot_deadline(void)
{
	init_timer(&lsd_boot_deadline);
	lsd_boot_deadline.function = lsd_boot_deadline_fn;
	lsd_boot_deadline.data = 0;
	lsd_boot_deadline.expires =
		jiffies + msecs_to_jiffies(LSD_BOOT_DEADLINE_MS);
	add_timer(&lsd_boot_deadline);
	return 0;
}
early_initcall(lsd_arm_boot_deadline);

static long lsd_seen;

/* msm-poweroff.c 的 restart prepare 读取：非0时改走 bootloader 热复位路径 */
int lsd_diag_fastboot;

/* lsd_death_check 在主动 panic 前置位：本次 console 记录因此附带"上次死亡阶段" */
int lsd_prev_stage;
u32 lsd_prev_initcall_fn;
int lsd_prev_died;	/* 上次启动是否静默卡死(1=是) */

/*
 * 开机第一条 C 代码（stage20 之前）调用。读"墓穴"：它由本次启动的汇编
 * stage10 从 IMEM 搬入，保存的是上一次启动的终止阶段。
 *
 * 策略调整: 不再立即 panic。记录死亡 stage/initcall_fn 到全局变量,
 * 让内核继续启动。kthread(UFS 就绪后)把这些信息写进 param 分区,
 * 若本次能正常进系统, 即可 adb 读 param 得到上次卡死位置。
 * 若本次也卡死, watchdog 仍会 panic, 信息保留在 IMEM 墓穴供下次读取。
 */
void lsd_death_check(void)
{
	u8 *b = (u8 *)__phys_to_virt(LSD_BS_IMEM_PHYS);
	u32 magic, stage;

	magic = *(u32 *)(b + LSD_BS_GRAVE_MAGIC_OFF);
	stage = *(u32 *)(b + LSD_BS_GRAVE_STAGE_OFF);

	if (magic == LSD_BS_GRAVE_MAGIC_VAL && stage && stage < LSD_BS_STAGE_GOOD) {
		lsd_prev_stage = (int)stage;
		lsd_prev_initcall_fn = *(u32 *)(b + LSD_BS_INITCALL_FN_OFF);
		lsd_prev_died = 1;
		/* 消费掉墓穴, 避免无限循环 */
		*(u32 *)(b + LSD_BS_GRAVE_MAGIC_OFF) = LSD_BS_MAGIC_DONE;
		wmb();
		pr_emerg("LSD: previous boot died at stage %u, initcall_fn=0x%x, continuing boot to dump via param\n",
			 stage, lsd_prev_initcall_fn);
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

/* ---- param 分区写入器 ----
 * 把 boot stage + 控制台末尾写入 param 分区(UFS 持久存储), b 槽可 dd 读出。
 * 关键设计: 写入函数可在任意上下文(含定时器软中断)调用, 用异步 bio + mdelay。
 * bdev 在 late_initcall 预打开(进程上下文可睡眠); 若晚于卡死点则定时器直接
 * 跳过写入(仅留 IMEM stage, 下次启动靠 lsd_death_check 读出)。 */

struct lsd_param_rec {
	u32 magic;
	u32 cur_stage;
	u32 grave_stage;
	u32 kaslr_offset;
	u32 restart_reason;
	u32 prev_died;		/* 上次是否卡死 */
	u32 prev_stage;		/* 上次死亡 stage */
	u32 prev_initcall_fn;	/* 上次卡死时正在执行的 initcall 函数指针 */
	u32 cur_initcall_fn;	/* 当前正在执行的 initcall 函数指针 */
	char last_console[256];
};

static struct block_device *lsd_param_bdev;
static struct page *lsd_param_page;

static void lsd_fill_record(struct lsd_param_rec *rec)
{
	u8 *imem = (u8 *)__phys_to_virt(LSD_BS_IMEM_PHYS);

	memset(rec, 0, 4096);
	rec->magic = LSD_PARAM_MAGIC;
	rec->cur_stage = *(u32 *)(imem + LSD_BS_STAGE_OFF);
	rec->grave_stage = *(u32 *)(imem + LSD_BS_GRAVE_STAGE_OFF);
	rec->kaslr_offset = *(u32 *)(imem + 0x6d0);
	rec->restart_reason = *(u32 *)(imem + 0x65c);
	rec->prev_died = lsd_prev_died;
	rec->prev_stage = lsd_prev_stage;
	rec->prev_initcall_fn = lsd_prev_initcall_fn;
	rec->cur_initcall_fn = *(u32 *)(imem + LSD_BS_INITCALL_FN_OFF);

	/* 抓 ramoops console 区末尾(可能含上次 panic 记录) */
	{
		struct pram *pr = (struct pram *)__phys_to_virt(LSD_CONSOLE_PHYS);
		if (pr->sig == DBGC && pr->size > 0 && pr->size < LSD_TXT_SZ) {
			int copylen = pr->size < (s32)sizeof(rec->last_console)
				      ? pr->size : (s32)sizeof(rec->last_console);
			memcpy(rec->last_console, pr->data + pr->size - copylen, copylen);
		}
	}
}

/* 可在任意上下文调用: 异步提交 bio, mdelay 忙等完成 */
static void lsd_param_write_now(void)
{
	struct bio *bio;
	struct lsd_param_rec *rec;

	if (!lsd_param_bdev || !lsd_param_page)
		return;

	rec = (struct lsd_param_rec *)page_address(lsd_param_page);
	lsd_fill_record(rec);
	__flush_dcache_area(rec, 4096);

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio)
		return;
	bio->bi_bdev = lsd_param_bdev;
	bio->bi_opf = REQ_OP_WRITE;
	bio->bi_iter.bi_sector = LSD_PARAM_SECTOR;
	if (bio_add_page(bio, lsd_param_page, 4096, 0) < 4096) {
		bio_put(bio);
		return;
	}
	submit_bio(bio);
	/* 忙等: UFS 控制器独立于 CPU, 即使内核线程全卡死也能完成 IO */
	mdelay(50);
}

static struct task_struct *lsd_param_thread;

static int lsd_param_kthread(void *data)
{
	while (!kthread_should_stop()) {
		/* 打开 bdev(UFS 未就绪时重试, 可睡眠) */
		if (!lsd_param_bdev) {
			lsd_param_bdev = blkdev_get_by_path("/dev/block/sda4",
							    FMODE_WRITE | FMODE_READ, NULL);
			if (IS_ERR(lsd_param_bdev))
				lsd_param_bdev = blkdev_get_by_dev(MKDEV(8, 4),
								   FMODE_WRITE | FMODE_READ, NULL);
			if (IS_ERR(lsd_param_bdev)) {
				lsd_param_bdev = NULL;
			} else {
				pr_info("LSD: param bdev opened, ro=%d\n",
					bdev_read_only(lsd_param_bdev));
			}
		}
		if (!lsd_param_page)
			lsd_param_page = alloc_page(GFP_KERNEL);

		lsd_param_write_now();
		msleep(2000);
	}
	return 0;
}

/* core_initcall: kthreadd 已启动, 可创建独立线程。
 * 该线程独立于 init 线程, 即使 init 卡在驱动初始化也能持续写 param。 */
static int __init lsd_param_init(void)
{
	lsd_param_thread = kthread_run(lsd_param_kthread, NULL, "lsd_param");
	if (IS_ERR(lsd_param_thread)) {
		pr_warn("LSD: param kthread create failed\n");
		lsd_param_thread = NULL;
	}
	return 0;
}
core_initcall(lsd_param_init);
