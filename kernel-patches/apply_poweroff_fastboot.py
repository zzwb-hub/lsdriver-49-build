#!/usr/bin/env python3
# LSD_DIAG: 让 panic 重启改走 "bootloader" 路径 —— 清 dload magic、强制热复位、
# restart reason 0x77665500，使手机自动进 Fastboot 菜单且 RAM 不断电。
import sys

P = "drivers/power/reset/msm-poweroff.c"
t = open(P).read()
T = "\t"

# --- 1) 函数入口：声明标志、改 cmd、强制 warm ---
a1 = (
    "static void msm_restart_prepare(const char *cmd)\n"
    "{\n"
    + T + "bool need_warm_reset = false;\n"
)
r1 = (
    "static void msm_restart_prepare(const char *cmd)\n"
    "{\n"
    + T + "bool need_warm_reset = false;\n"
    + "/* LSD_DIAG */ extern int lsd_diag_fastboot;\n"
    + T + "if (lsd_diag_fastboot)\n"
    + T + T + "cmd = \"bootloader\";\n"
)
if a1 not in t:
    sys.exit("anchor1 not found (already patched?)")
t = t.replace(a1, r1, 1)

# --- 2) 清 dload magic（否则 aboot 仍进 CrashDump）---
a2 = (
    T + "set_dload_mode(download_mode &&\n"
    + T + T + "(in_panic || restart_mode == RESTART_DLOAD));\n"
)
r2 = (
    T + "if (lsd_diag_fastboot)\n"
    + T + T + "set_dload_mode(0);\n"
    + T + "else\n"
    + T + "set_dload_mode(download_mode &&\n"
    + T + T + "(in_panic || restart_mode == RESTART_DLOAD));\n"
)
if a2 not in t:
    sys.exit("anchor2 not found")
t = t.replace(a2, r2, 1)

# --- 3) 强制热复位，保证 RAM 不掉电 ---
a3 = (
    T + "if (force_warm_reboot)\n"
    + T + T + "pr_info(\"Forcing a warm reset of the system\\n\");\n"
)
r3 = (
    T + "if (lsd_diag_fastboot) {\n"
    + T + T + "need_warm_reset = true;\n"
    + T + T + "pr_info(\"LSD diag: forcing warm reset to fastboot\\n\");\n"
    + T + "}\n"
    + a3
)
if a3 not in t:
    sys.exit("anchor3 not found")
t = t.replace(a3, r3, 1)

open(P, "w").write(t)
print("msm-poweroff restart->bootloader injected")
