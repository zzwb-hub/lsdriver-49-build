#!/usr/bin/env python3
# LSD_DIAG: initcall level + count 二分法。
# lsd_initcall_max=N: 只跑前 N 个 level (0-7)
# lsd_initcall_count=M: 在 max level 内只跑前 M 个 initcall (99=全部)
import sys

P = "init/main.c"
t = open(P).read()
T = "\t"

# --- 1) 改 do_initcall_level: 在 max level 内限制 initcall 数量 ---
a1 = (
    "static void __init do_initcall_level(int level)\n"
    "{\n"
    + T + "initcall_t *fn;\n"
    + "\n"
    + T + "strcpy(initcall_command_line, saved_command_line);\n"
    + T + "parse_args(initcall_level_names[level],\n"
    + T + T + "   initcall_command_line, __start___param,\n"
    + T + T + "   __stop___param - __start___param,\n"
    + T + T + "   level, level,\n"
    + T + T + "   NULL, &repair_env_string);\n"
    + "\n"
    + T + "for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++)\n"
    + T + T + "do_one_initcall(*fn);\n"
    "}\n"
)
r1 = (
    "static int lsd_initcall_count_max = 99999; /* max level 内只跑前 N 个 initcall */\n"
    "\n"
    "static void __init do_initcall_level(int level)\n"
    "{\n"
    + T + "initcall_t *fn;\n"
    + T + "int cnt = 0;\n"
    + "\n"
    + T + "strcpy(initcall_command_line, saved_command_line);\n"
    + T + "parse_args(initcall_level_names[level],\n"
    + T + T + "   initcall_command_line, __start___param,\n"
    + T + T + "   __stop___param - __start___param,\n"
    + T + T + "   level, level,\n"
    + T + T + "   NULL, &repair_env_string);\n"
    + "\n"
    + T + "for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++) {\n"
    + T + T + "if (level == lsd_initcall_max_level && cnt >= lsd_initcall_count_max) {\n"
    + T + T + T + "pr_emerg(\"LSD: level %d initcall #%d skipped (count limit %d)\\n\",\n"
    + T + T + T + T + "level, cnt, lsd_initcall_count_max);\n"
    + T + T + T + "break;\n"
    + T + T + "}\n"
    + T + T + "do_one_initcall(*fn);\n"
    + T + T + "cnt++;\n"
    + T + "}\n"
    "}\n"
)
if a1 not in t:
    sys.exit("anchor1 not found (already patched?)")
t = t.replace(a1, r1, 1)

# --- 2) 在 do_initcalls 前加参数解析 ---
a2 = (
    "static void __init do_initcalls(void)\n"
    "{\n"
    + T + "int level;\n"
    + "\n"
    + T + "for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++)\n"
    + T + T + "do_initcall_level(level);\n"
    "}\n"
)
r2 = (
    "static int lsd_initcall_max_level = 99;\n"
    "static int __init lsd_set_initcall_max(char *str)\n"
    "{\n"
    + T + "int v;\n"
    + T + "if (str && kstrtoint(str, 0, &v) == 0 && v >= 0 && v <= 7)\n"
    + T + T + "lsd_initcall_max_level = v;\n"
    + T + "return 0;\n"
    "}\n"
    "early_param(\"lsd_initcall_max\", lsd_set_initcall_max);\n"
    "\n"
    "static int __init lsd_set_initcall_count(char *str)\n"
    "{\n"
    + T + "int v;\n"
    + T + "if (str && kstrtoint(str, 0, &v) == 0 && v >= 0)\n"
    + T + T + "lsd_initcall_count_max = v;\n"
    + T + "return 0;\n"
    "}\n"
    "early_param(\"lsd_initcall_count\", lsd_set_initcall_count);\n"
    "\n"
    "static void __init do_initcalls(void)\n"
    "{\n"
    + T + "int level;\n"
    + T + "int max_level = lsd_initcall_max_level;\n"
    + "\n"
    + T + "if (max_level > 7) max_level = 7;\n"
    + T + "pr_emerg(\"LSD: running initcall levels 0..%d, count_limit=%d\\n\",\n"
    + T + T + "max_level, lsd_initcall_count_max);\n"
    + T + "for (level = 0; level <= max_level; level++)\n"
    + T + T + "do_initcall_level(level);\n"
    + T + "pr_emerg(\"LSD: initcall levels done\\n\");\n"
    "}\n"
)
if a2 not in t:
    sys.exit("anchor2 not found")
t = t.replace(a2, r2, 1)

open(P, "w").write(t)
print("initcall bisect injected")
