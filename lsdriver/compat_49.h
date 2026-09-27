#ifndef _COMPAT_49_H_
#define _COMPAT_49_H_

/*
 * 内核 4.9 兼容层（一加6 enchilada / 氢OS Android 10 / 4.9.179-perf+）
 *
 * 本头自包含：被 arm64_reg.h / export_fun.h 在文件顶部引入，
 * 保证业务头无论被谁先包含（如 io_struct.h 先于 export_fun.h），shim 都已就位。
 * 所有 shim 仅在内核 < 对应引入版本时生效，新内核原逻辑不受影响。
 */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h> /* pud_huge/pmd_huge 原型或 fallback 宏；正确顺序包含 asm/hugetlb.h */
#include <asm/pgtable.h>
#include <asm/sysreg.h>
#include <asm/memory.h>

/* ---- sysreg_clear_set：4.14 引入。4.9 的 read/write_sysreg 直传寄存器名到汇编
 *      （小写 mdscr_el1 合法，见 asm/assembler.h），直接复用 ---- */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
#define sysreg_clear_set(reg, clear, set) do {          \
        uint64_t __v = (uint64_t)read_sysreg(reg);      \
        __v &= ~(uint64_t)(clear);                     \
        __v |= (uint64_t)(set);                        \
        write_sysreg(__v, reg);                         \
    } while (0)
#endif

/* ---- arm_smccc_conduit 枚举：4.16 才有。驱动只按 HVC/SMC 二选一，值序自定义自洽 ---- */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
enum arm_smccc_conduit { SMCCC_CONDUIT_HVC, SMCCC_CONDUIT_SMC };
#endif

/* ---- 4.9 arm-smccc.h 无 VENDOR_HYP owner（规范值 6，位于 STANDARD=4 与 TRUSTED_APP=48 之间） ---- */
#ifndef ARM_SMCCC_OWNER_VENDOR_HYP
#define ARM_SMCCC_OWNER_VENDOR_HYP 6
#endif

/* ---- clone3：5.3 新增。4.9 上 435 号无对应调用，case 为死分支，语义安全 ---- */
#ifndef __NR_clone3
#define __NR_clone3 435
#endif

/* ---- fallthrough 伪语句：5.9 才有。旧编译器退化为空语句（仅表"故意贯穿"意图） ---- */
#ifndef fallthrough
#define fallthrough do {} while (0)
#endif

/* ---- ESR_ELx_FSC_LEVEL：故障状态中的翻译级位（与 PERM=0xC 组合 = 0xD 即 L3 权限故障） ---- */
#ifndef ESR_ELx_FSC_LEVEL
#define ESR_ELx_FSC_LEVEL (0x3)
#endif

/* ---- PTE_MAYBE_GP：BTI Guard Page 位，4.9 无 BTI → 0 ---- */
#ifndef PTE_MAYBE_GP
#define PTE_MAYBE_GP 0
#endif

/* ---- VMA 访问位/清理位（5.x 引入）；4.9 无 pkey，CLEAR=0 即只清 rwx ---- */
#ifndef VM_ACCESS_FLAGS
#define VM_ACCESS_FLAGS (VM_READ | VM_WRITE | VM_EXEC)
#endif
#ifndef VM_FLAGS_CLEAR
#define VM_FLAGS_CLEAR 0
#endif

/* ---- __is_lm_address：5.4+ 精确版；4.9 线性映射从 PAGE_OFFSET 开始，比较下界即可 ---- */
#ifndef __is_lm_address
#define __is_lm_address(addr) ((uint64_t)(addr) >= (uint64_t)PAGE_OFFSET)
#endif

/* ---- uaccess_*_privileged：4.14 PAN 重写引入。4.9 对应 uaccess_enable/disable
 *      （enable=清 PAN 允许内核访问用户内存，disable=置 PAN 恢复隔离） ---- */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
#define uaccess_enable_privileged()  uaccess_enable()
#define uaccess_disable_privileged() uaccess_disable()
#endif

/* ---- access_ok：5.0 去掉 type 参数，包装一层 ---- */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
#define lsd_access_ok(addr, size) access_ok(VERIFY_WRITE, (addr), (size))
#else
#define lsd_access_ok(addr, size) access_ok((addr), (size))
#endif

/* ---- sched_set_fifo[_low]：5.7 才有，FIFO 优先级 1 / MAX_RT_PRIO/2 ---- */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
static inline int lsd_sched_set_fifo_low(struct task_struct *p)
{
    struct sched_param sp = {.sched_priority = 1};
    return sched_setscheduler_nocheck(p, SCHED_FIFO, &sp);
}
static inline int lsd_sched_set_fifo(struct task_struct *p)
{
    struct sched_param sp = {.sched_priority = MAX_RT_PRIO / 2};
    return sched_setscheduler_nocheck(p, SCHED_FIFO, &sp);
}
#define sched_set_fifo_low lsd_sched_set_fifo_low
#define sched_set_fifo lsd_sched_set_fifo
#endif

/* ---- find_task_by_vpid：4.9 有函数但未 EXPORT_SYMBOL（modpost 报 undefined）。
 *      用两个均已导出的 API 等价组合替代：
 *      find_task_by_vpid(nr) == pid_task(find_vpid(nr), PIDTYPE_PID) ---- */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
static inline struct task_struct *lsd_find_task_by_vpid(pid_t nr)
{
    return pid_task(find_vpid(nr), PIDTYPE_PID);
}
#define find_task_by_vpid lsd_find_task_by_vpid
#endif

/* ---- mmap_sem：5.8 改名 mpa_lock 并配套读写锁 API ---- */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0)
#define mmap_read_lock(mm)        down_read(&(mm)->mmap_sem)
#define mmap_read_unlock(mm)      up_read(&(mm)->mmap_sem)
#define mmap_write_lock(mm)       down_write(&(mm)->mmap_sem)
#define mmap_write_unlock(mm)     up_write(&(mm)->mmap_sem)
/* down_write_killable 4.13 才有；4.9 退化为不可杀写锁 */
#define mmap_write_lock_killable(mm) (down_write(&(mm)->mmap_sem), 0)
#endif

/* ---- p4d 页表层：4.11 引入。4.9 arm64 为 pgd→pud 直走，p4d 恒等映射 ---- */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
typedef pgd_t p4d_t;
#define p4d_offset(pgd, addr) (pgd)
#define p4d_none(p4d)         (0)
#define p4d_bad(p4d)          (0)
#define p4d_present(p4d)      (1)
#define p4d_val(p4d)          pgd_val(p4d)
#define __p4d(x)              __pgd(x)
/* pud_leaf/pmd_leaf 后引入；4.9 对应 pud_huge/pmd_huge */
#define pud_leaf(pud)          pud_huge(pud)
#define pmd_leaf(pmd)          pmd_huge(pmd)

/* ---- __TLBI_VADDR：4.10 随 tlbflush 重写引入。
 *      去掉页内偏移转 TLBI 操作数 VA[43:12]，ASID 放高位 ---- */
#define __TLBI_VADDR(addr, asid)                            \
    ({                                                       \
        uint64_t __ta = ((uint64_t)(addr)) >> 12;           \
        __ta &= (uint64_t)GENMASK_ULL(43, 0);               \
        __ta |= ((uint64_t)(asid)) << 48;                   \
        __ta;                                                \
    })
#endif

#endif /* _COMPAT_49_H_ */
