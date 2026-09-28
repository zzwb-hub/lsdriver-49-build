#!/usr/bin/env python3
# LSD_DIAG: 在 arch/arm64/kernel/head.S 的 stext（MMU 开启前）各关键阶段
# 往 IMEM 写进度编号。MMU 开启后不能再用物理地址直写，故只打到 __cpu_setup。
import sys

P = "arch/arm64/kernel/head.S"
t = open(P).read()
T = "\t"
MARK = "LSD_BOOTSTAGE_ASM"

if MARK in t:
    print("already patched")
    sys.exit(0)

macro = (
    "/* " + MARK + ": boot-stage markers in IMEM phys 0x146bf000 (MMU off) */\n"
    # first marker of a boot: preserve previous boot's stage into the grave
    + "/* first marker: copy prior stage into grave before overwriting */\n"
    + ".macro\tlsd_stage_first code\n"
    + "movz\tx9, #0xf000\n"
    + "movk\tx9, #0x146b, lsl #16\n"
    + "ldr\tw10, [x9, #0x700]\n"
    + "movz\tw11, #0xb007\n"
    + "movk\tw11, #0xb007, lsl #16\n"
    + "cmp\tw10, w11\n"
    + "b.ne\t98f\n"
    + "ldr\tw12, [x9, #0x708]\n"
    + "movz\tw10, #0x4156\n"
    + "movk\tw10, #0x4752, lsl #16\n"
    + "str\tw10, [x9, #0x70c]\n"
    + "str\tw12, [x9, #0x710]\n"
    + "98:\n"
    + "str\tw11, [x9, #0x700]\n"
    + "mov\tw10, #\\code\n"
    + "str\tw10, [x9, #0x708]\n"
    + "dsb\tsy\n"
    + ".endm\n"
    # normal marker
    + ".macro\tlsd_stage code\n"
    + "movz\tx9, #0xf000\n"
    + "movk\tx9, #0x146b, lsl #16\n"
    + "movz\tx10, #0xb007\n"
    + "movk\tx10, #0xb007, lsl #16\n"
    + "str\tw10, [x9, #0x700]\n"
    + "mov\tw10, #\\code\n"
    + "str\tw10, [x9, #0x708]\n"
    + "dsb\tsy\n"
    + ".endm\n"
)

# 1) macros + stext 10(first)/11/12
old1 = (
    "ENTRY(stext)\n"
    + T + "bl\tpreserve_boot_args\n"
    + T + "bl\tel2_setup\t\t\t// Drop to EL1, w0=cpu_boot_mode\n"
)
new1 = (
    macro
    + "ENTRY(stext)\n"
    + T + "lsd_stage_first\t10\n"
    + T + "bl\tpreserve_boot_args\n"
    + T + "lsd_stage\t11\n"
    + T + "bl\tel2_setup\t\t\t// Drop to EL1, w0=cpu_boot_mode\n"
    + T + "lsd_stage\t12\n"
)
if old1 not in t:
    sys.exit("anchor1 not found")
t = t.replace(old1, new1, 1)

# 2) 13/14
old2 = (
    T + "bl\tset_cpu_boot_mode_flag\n"
    + T + "bl\t__create_page_tables\n"
)
new2 = (
    T + "bl\tset_cpu_boot_mode_flag\n"
    + T + "lsd_stage\t13\n"
    + T + "bl\t__create_page_tables\n"
    + T + "lsd_stage\t14\n"
)
if old2 not in t:
    sys.exit("anchor2 not found")
t = t.replace(old2, new2, 1)

# 3) 15
old3 = (
    T + "bl\t__cpu_setup\t\t\t// initialise processor\n"
    + T + "b\t__primary_switch\n"
)
new3 = (
    T + "bl\t__cpu_setup\t\t\t// initialise processor\n"
    + T + "lsd_stage\t15\n"
    + T + "b\t__primary_switch\n"
)
if old3 not in t:
    sys.exit("anchor3 not found")
t = t.replace(old3, new3, 1)

# 4) probe right before "b start_kernel" (MMU on, C callable). Determines
# whether execution reaches the end of __primary_switched at all.
old4 = (
    T + "b\tstart_kernel\n"
    + "ENDPROC(__primary_switched)\n"
)
new4 = (
    T + "ldr_l\tx8, lsd_probe\n"
    + T + "blr\tx8\n"
    + T + "b\tstart_kernel\n"
    + "ENDPROC(__primary_switched)\n"
)
if old4 not in t:
    sys.exit("anchor4 not found")
t = t.replace(old4, new4, 1)

open(P, "w").write(t)
print("head.S stages 10-15 + pre-start_kernel probe injected")
