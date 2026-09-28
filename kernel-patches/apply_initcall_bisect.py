#!/usr/bin/env python3
# LSD_DIAG: initcall level 二分法。
# 加内核参数 lsd_initcall_max=N，只跑前 N 个 initcall level。
# 用法: 内核命令行加 lsd_initcall_max=5 即只跑到 fs level，不跑 device/late。
import sys

P = "init/main.c"
t = open(P).read()
T = "\t"

# --- 1) 在 do_initcalls 前加全局变量和参数解析 ---
a1 = (
    "static void __init do_initcalls(void)\n"
    "{\n"
    + T + "int level;\n"
    + "\n"
    + T + "for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++)\n"
    + T + T + "do_initcall_level(level);\n"
    "}\n"
)
r1 = (
    "static int lsd_initcall_max_level = 99; /* 默认跑全部 level */\n"
    "static int __init lsd_set_initcall_max(char *str)\n"
    "{\n"
    + T + "int v;\n"
    + T + "if (str && kstrtoint(str, 0, &v) == 0 && v >= 0 && v <= 7) {\n"
    + T + T + "lsd_initcall_max_level = v;\n"
    + T + T + "pr_emerg(\"LSD: initcall bisect: max level=%d (%s)\\n\", v,\n"
    + T + T + T + "v < 8 ? initcall_level_names[v] : \"all\");\n"
    + T + "}\n"
    + T + "return 0;\n"
    "}\n"
    "early_param(\"lsd_initcall_max\", lsd_set_initcall_max);\n"
    "\n"
    "static void __init do_initcalls(void)\n"
    "{\n"
    + T + "int level;\n"
    + T + "int max_level = lsd_initcall_max_level;\n"
    + "\n"
    + T + "if (max_level > 7) max_level = 7;\n"
    + T + "pr_emerg(\"LSD: running initcall levels 0..%d\\n\", max_level);\n"
    + T + "for (level = 0; level <= max_level; level++)\n"
    + T + T + "do_initcall_level(level);\n"
    + T + "pr_emerg(\"LSD: initcall levels 0..%d done, skipping rest\\n\", max_level);\n"
    "}\n"
)
if a1 not in t:
    sys.exit("anchor1 not found (already patched?)")
t = t.replace(a1, r1, 1)

open(P, "w").write(t)
print("initcall bisect injected")
