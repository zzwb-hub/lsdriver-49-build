// SPDX-License-Identifier: GPL-2.0
// LSD_DIAG imem_reader - 在正常系统(b槽)读取 IMEM 中的启动阶段标记。
// 用法：
//   insmod imem_reader.ko                 只读
//   insmod imem_reader.ko wstage=42       写magic+42（用于验证IMEM冷启动保持性）
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>

#define IMEM_PHYS	0x146bf000UL
#define OFF_MAGIC	0x700
#define OFF_STAGE	0x708
#define MAGIC		0xB007B007U

static int wstage = -1;
module_param(wstage, int, 0644);
MODULE_PARM_DESC(wstage, "write this stage to IMEM (magic auto set); -1 = read only");

static void __iomem *m;

static int __init ir_init(void)
{
	u32 magic, stage, rr, kaslr;

	m = ioremap(IMEM_PHYS, 0x1000);
	if (!m)
		return -ENOMEM;

	if (wstage >= 0) {
		writel(MAGIC, m + OFF_MAGIC);
		writel((u32)wstage, m + OFF_STAGE);
		wmb();
		mdelay(2);
	}

	magic = readl(m + OFF_MAGIC);
	stage = readl(m + OFF_STAGE);
	rr = readl(m + 0x65c);
	kaslr = readl(m + 0x6d0);

	pr_info("IMEM_READER magic=0x%08x stage=0x%08x restart_reason=0x%08x kaslr=0x%08x\n",
		magic, stage, rr, kaslr);
	return 0;
}

static void __exit ir_exit(void)
{
	if (m)
		iounmap(m);
}

module_init(ir_init);
module_exit(ir_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LSD_DIAG: read boot-stage marker from IMEM");
