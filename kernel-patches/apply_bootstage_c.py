#!/usr/bin/env python3
# LSD_DIAG: 在 init/main.c 的 start_kernel/rest_init 各关键阶段往 IMEM 写进度。
import sys

P = "init/main.c"
t = open(P).read()
T = "\t"
MARK = "lsd_boot_stage"

if MARK in t:
    print("already patched")
    sys.exit(0)

def rep(old, new, tag):
    global t
    if old not in t:
        sys.exit("anchor not found: " + tag)
    if t.count(old) != 1:
        sys.exit("anchor not unique: " + tag)
    t = t.replace(old, new, 1)

# include at the top header block (must precede rest_init(), which sits
# before start_kernel and also calls lsd_boot_stage)
rep(
    "#include <linux/types.h>\n",
    "#include <linux/types.h>\n"
    "#include <linux/lsd_bootstage.h>\n",
    "top include",
)

# death self-check, then stage 20 (unconditional probe removed for hang hunt)
rep(
    "asmlinkage __visible void __init start_kernel(void)\n"
    "{\n"
    + T + "char *command_line;\n"
    + T + "char *after_dashes;\n",
    "asmlinkage __visible void __init start_kernel(void)\n"
    "{\n"
    + T + "char *command_line;\n"
    + T + "char *after_dashes;\n\n"
    + T + "lsd_death_check();\n"
    + T + "lsd_boot_stage(20);\n",
    "start_kernel head",
)

# 21..26 one-liners after key calls
for call, code in [
    ("setup_arch(&command_line);", 21),
    ("mm_init();", 22),
    ("sched_init();", 23),
    ("rcu_init();", 24),
    ("init_IRQ();", 25),
    ("timekeeping_init();", 26),
]:
    rep(
        T + call + "\n",
        T + call + "\n" + T + "lsd_boot_stage(%d);\n" % code,
        call,
    )

# 27 after the boot local_irq_enable (followed by kmem_cache_init_late)
rep(
    T + "local_irq_enable();\n\n"
    + T + "kmem_cache_init_late();\n",
    T + "local_irq_enable();\n"
    + T + "lsd_boot_stage(27);\n\n"
    + T + "kmem_cache_init_late();\n",
    "local_irq_enable boot",
)

# 28 before rest_init()
rep(
    T + "rest_init();\n",
    T + "lsd_boot_stage(28);\n"
    + T + "rest_init();\n",
    "rest_init call",
)

# 29 at rest_init entry
rep(
    "static noinline void __ref rest_init(void)\n"
    "{\n"
    + T + "int pid;\n",
    "static noinline void __ref rest_init(void)\n"
    "{\n"
    + T + "int pid;\n\n"
    + T + "lsd_boot_stage(29);\n",
    "rest_init entry",
)

# 40 in kernel_init, right before executing userspace init; reaching here means
# all driver initcalls completed -> cancel the boot-progress deadline.
rep(
    T + "rcu_end_inkernel_boot();\n\n"
    + T + "if (ramdisk_execute_command) {\n",
    T + "rcu_end_inkernel_boot();\n\n"
    + T + "lsd_boot_stage(40);\n"
    + T + "lsd_boot_ok();\n"
    + T + "if (ramdisk_execute_command) {\n",
    "kernel_init stage 40",
)

open(P, "w").write(t)
print("main.c boot stages 20-40 injected")
