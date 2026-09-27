#!/usr/bin/env python3
# 修复真机早期 "Kernel BUG at kfree / free_task"（rcuop 回收内核线程时触发）。
#
# 根因：公开树删除了 oneplus/coretech，没有任何 Kconfig 定义 config OPCHAIN，
# 故 defconfig 里的 CONFIG_OPCHAIN=y 被 olddefconfig 丢弃（变为未定义）。
# 但 task_struct->nn(struct nedf_node*) 的分配(copy_process)/释放(free_task)/
# exit 置位都没有用 CONFIG_OPCHAIN 保护，唯一的 tsk->nn = NULL 却被包在
# dup_task_struct 的 #ifdef CONFIG_OPCHAIN 里。结果内核线程的 nn 从不置空，
# 是垃圾值；第一个内核线程退出、经 RCU 延迟回收时 free_task 解引用垃圾 nn，
# kfree(野指针) -> BUG。
#
# 修法：把 tsk->nn = NULL 移到 #ifdef 之外，无条件置空。
import sys

P = "kernel/fork.c"
t = open(P).read()
T = "\t"

MARK = "LSD_FIX_NN_INIT"
if MARK in t:
    print("already patched")
    sys.exit(0)

old = (
    T + "/*Curtis, 20180425, non-exist dcache*/\n"
    + T + "tsk->nn = NULL;\n"
    + "#endif\n"
)
if old not in t:
    sys.exit("anchor not found (dup_task_struct OPCHAIN block changed?)")

new = (
    "#endif\n"
    + "/* " + MARK + ": public tree has no config OPCHAIN (dropped by olddefconfig),\n"
    + " * yet tsk->nn is alloc/freed unguarded. Zero it unconditionally so reaped\n"
    + " * kernel threads don't kfree garbage (Kernel BUG at kfree in free_task). */\n"
    + T + "tsk->nn = NULL;\n"
)
t = t.replace(old, new, 1)
open(P, "w").write(t)
print("nn unconditional init injected")
