#!/usr/bin/env python3
# LSD_DIAG: 在 arch/arm64/kernel/traps.c 的 oops_end() 中、panic 之前，
# 调用 lsd_capture(regs)，把中断致命异常的寄存器现场/栈直接写进 ramoops console 区。
import sys

P = "arch/arm64/kernel/traps.c"
t = open(P).read()
T = "\t"

anchor = (
    T + "oops_exit();\n\n"
    + T + "if (in_interrupt())\n"
)
if anchor not in t:
    sys.exit("anchor not found (already patched?)")

inject = (
    T + "oops_exit();\n\n"
    + "/* LSD_DIAG: panic 前现场(regs)尚在，直接写 ramoops console 区 */\n"
    + "extern void lsd_capture(struct pt_regs *regs);\n"
    + "lsd_capture(regs);\n\n"
    + T + "if (in_interrupt())\n"
)
t = t.replace(anchor, inject, 1)
open(P, "w").write(t)
print("traps lsd_capture injected")
