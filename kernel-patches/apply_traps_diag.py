#!/usr/bin/env python3
# LSD_DIAG: 修改 arch/arm64/kernel/traps.c
#   中断上下文致命异常默认 panic("Fatal exception in interrupt")，在极早期会立刻
#   进 Qualcomm CrashDump，pstore/fbcon 都来不及工作。诊断构建里改为：打印 oops 栈后
#   不 panic、直接返回，尽力让系统继续启动到用户态，从而能用 adb 从 dmesg 读到崩溃栈。
import sys

P = "arch/arm64/kernel/traps.c"
t = open(P).read()
T = "\t"

# 1) 文件顶部打开诊断开关
anchor1 = " */\n\n#include <linux/bug.h>"
if anchor1 not in t:
    sys.exit("anchor1 not found (already patched?)")
t = t.replace(
    anchor1,
    " */\n\n"
    "/* LSD_DIAG: 中断内致命异常只记录不 panic，争取启动到用户态读 dmesg 栈 */\n"
    "#define LSD_DIAG_SUPPRESS_OOPS_PANIC 1\n\n"
    "#include <linux/bug.h>",
    1,
)

# 2) oops_end 中的 panic 逻辑
old = (
    T + "oops_exit();\n\n"
    + T + "if (in_interrupt())\n"
    + T * 2 + 'panic("Fatal exception in interrupt");\n'
    + T + "if (panic_on_oops)\n"
    + T * 2 + 'panic("Fatal exception");\n'
    + T + "if (notify != NOTIFY_STOP)\n"
    + T * 2 + "do_exit(SIGSEGV);"
)
if old not in t:
    sys.exit("anchor2 not found")

new = (
    T + "oops_exit();\n\n"
    + "#ifdef LSD_DIAG_SUPPRESS_OOPS_PANIC\n"
    + T + "if (in_interrupt()) {\n"
    + T * 2 + 'pr_emerg("LSD_DIAG: suppressed panic for fatal exception in interrupt\\n");\n'
    + T * 2 + "/* 中断上下文无法 do_exit；直接返回，尽力继续启动以便读取本条 oops 栈 */\n"
    + T * 2 + "return;\n"
    + T + "}\n"
    + T + "if (panic_on_oops)\n"
    + T * 2 + 'pr_emerg("LSD_DIAG: suppressed panic_on_oops panic\\n");\n'
    + "#else\n"
    + T + "if (in_interrupt())\n"
    + T * 2 + 'panic("Fatal exception in interrupt");\n'
    + T + "if (panic_on_oops)\n"
    + T * 2 + 'panic("Fatal exception");\n'
    + "#endif\n"
    + T + "if (notify != NOTIFY_STOP)\n"
    + T * 2 + "do_exit(SIGSEGV);"
)
t = t.replace(old, new, 1)

open(P, "w").write(t)
print("traps diag applied")
