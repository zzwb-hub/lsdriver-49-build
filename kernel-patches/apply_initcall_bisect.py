#!/usr/bin/env python3
# LSD_DIAG: initcall 二分法（level 限制 + level 内 count 限制）。
#   lsd_initcall_max=N   : 只跑前 N 个 level (0-7)
#   lsd_initcall_count=M : 在 max level 内只跑前 M 个 initcall，其余跳过
#
# 设计: 只改 do_one_initcall(加计数+跳过) 和 do_initcalls(level 限制+计数复位)，
# 不碰 do_initcall_level 的循环体 —— 后者是 apply_bootstage_c.py 的 anchor，
# 若在此改写会导致其 anchor not found。
import sys

P = "init/main.c"
t = open(P).read()
T = "\t"

if "lsd_icount" in t:
    print("already patched")
    sys.exit(0)


def rep(old, new, tag):
    global t
    if old not in t:
        sys.exit("anchor not found: " + tag)
    if t.count(old) != 1:
        sys.exit("anchor not unique: " + tag)
    t = t.replace(old, new, 1)


# --- 1) 在 do_one_initcall 定义前插入全局变量与内核参数 ---
rep(
    "int __init_or_module do_one_initcall(initcall_t fn)\n",
    "/* LSD_DIAG: initcall 二分法全局状态 */\n"
    "static int lsd_icount;\n"
    "static int lsd_icount_max = 99999;\n"
    "static int lsd_initcall_max_level = 99;\n"
    "static int __init lsd_set_initcall_max(char *str)\n"
    "{\n"
    + T + "int v;\n"
    + T + "if (str && !kstrtoint(str, 0, &v) && v >= 0 && v <= 7)\n"
    + T + T + "lsd_initcall_max_level = v;\n"
    + T + "return 0;\n"
    "}\n"
    "early_param(\"lsd_initcall_max\", lsd_set_initcall_max);\n"
    "static int __init lsd_set_initcall_count(char *str)\n"
    "{\n"
    + T + "int v;\n"
    + T + "if (str && !kstrtoint(str, 0, &v) && v >= 0)\n"
    + T + T + "lsd_icount_max = v;\n"
    + T + "return 0;\n"
    "}\n"
    "early_param(\"lsd_initcall_count\", lsd_set_initcall_count);\n"
    "static int lsd_skip_lo = -1;\n"
    "static int lsd_skip_hi = -1;\n"
    "static int __init lsd_set_skip_lo(char *str)\n"
    "{\n"
    + T + "int v;\n"
    + T + "if (str && !kstrtoint(str, 0, &v))\n"
    + T + T + "lsd_skip_lo = v;\n"
    + T + "return 0;\n"
    "}\n"
    "early_param(\"lsd_skip_lo\", lsd_set_skip_lo);\n"
    "static int __init lsd_set_skip_hi(char *str)\n"
    "{\n"
    + T + "int v;\n"
    + T + "if (str && !kstrtoint(str, 0, &v))\n"
    + T + T + "lsd_skip_hi = v;\n"
    + T + "return 0;\n"
    "}\n"
    "early_param(\"lsd_skip_hi\", lsd_set_skip_hi);\n"
    "\n"
    "int __init_or_module do_one_initcall(initcall_t fn)\n",
    "globals before do_one_initcall",
)

# --- 2) do_one_initcall 顶部加计数+跳过（声明之后、第一条语句之前，兼容 gnu89）---
rep(
    "int count = preempt_count();\n"
    + T + "int ret;\n"
    + T + "char msgbuf[64];\n"
    + "\n"
    + T + "if (initcall_blacklisted(fn))\n",
    "int count = preempt_count();\n"
    + T + "int ret;\n"
    + T + "char msgbuf[64];\n"
    + "\n"
    + T + "if (lsd_icount >= lsd_icount_max) {\n"
    + T + T + "lsd_icount++;\n"
    + T + T + "return 0;\n"
    + T + "}\n"
    + T + "if (lsd_skip_hi >= 0 && lsd_icount >= lsd_skip_lo && lsd_icount < lsd_skip_hi) {\n"
    + T + T + "lsd_icount++;\n"
    + T + T + "return 0;\n"
    + T + "}\n"
    + T + "lsd_icount++;\n"
    + "\n"
    + T + "if (initcall_blacklisted(fn))\n",
    "do_one_initcall guard",
)

# --- 3) do_initcalls: level 限制 + 进入 max level 前复位计数 ---
rep(
    "static void __init do_initcalls(void)\n"
    "{\n"
    + T + "int level;\n"
    + "\n"
    + T + "for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++)\n"
    + T + T + "do_initcall_level(level);\n"
    "}\n",
    "static void __init do_initcalls(void)\n"
    "{\n"
    + T + "int level;\n"
    + T + "int max_level = lsd_initcall_max_level;\n"
    + "\n"
    + T + "if (max_level > 7)\n"
    + T + T + "max_level = 7;\n"
    + T + "pr_emerg(\"LSD: initcall levels 0..%d count_max=%d\\n\",\n"
    + T + T + "max_level, lsd_icount_max);\n"
    + T + "for (level = 0; level <= max_level; level++) {\n"
    + T + T + "if (level == max_level)\n"
    + T + T + T + "lsd_icount = 0;\n"
    + T + T + "do_initcall_level(level);\n"
    + T + "}\n"
    + T + "pr_emerg(\"LSD: initcall bisect done\\n\");\n"
    "}\n",
    "do_initcalls level limit",
)

open(P, "w").write(t)
print("initcall bisect injected (do_one_initcall + do_initcalls)")