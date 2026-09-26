# lsdriver-49-build

lsdriver 内核驱动的 **一加6（enchilada，氢OS Android 10，内核 4.9.179）适配分支**，通过 GitHub Actions 云编译产出 `lsdriver.ko`。

上游源码来自 `D:\do_work\一加手机\lsdriver_src\`（保持原始未改），本分支的 4.9 适配补丁全部在 `lsdriver/` 目录内，补丁点均有注释标注。

## 发布与编译步骤

1. 用 GitHub Desktop 打开本目录 → **Publish repository**（建议 Private）。
2. GitHub 网页进入仓库 → **Actions** → **build-lsdriver-4.9** → **Run workflow**。
   - `localversion` 输入框：先在手机上跑 `adb shell uname -r`，把 `4.9.179` 后面的后缀原样填入（如 `-perf+`）；无后缀就留空。
3. 等运行完成，在运行记录页下载 **lsdriver-ko** 构件，解压得 `lsdriver.ko`。
4. 推送到手机：`adb push lsdriver.ko /data/local/tmp/`，然后 `adb shell su -c 'insmod /data/local/tmp/lsdriver.ko'`。

## insmod 失败排查

| 报错 | 原因 | 处理 |
|---|---|---|
| `invalid module format`，dmesg 提示 vermagic 不符 | LOCALVERSION 没对齐 | 按 dmesg 里 `should be '...'` 的字符串修正 localversion 输入重跑 |
| `no symbol version for XXX` / `disagrees about version` | 内核开了 MODVERSIONS，CRC 不符 | workflow 会自动整树编译，一般不会出；出了把 Actions 日志发给维护者 |
| `Unknown symbol kgsl_process_init_sysfs` 等 | 厂商符号与真机不符 | 真机 `adb shell su -c 'grep 符号名 /proc/kallsyms'` 确认后再改 |

## 已知留白（4.9 上的能力边界）

- **syscall monitor 不可用**：4.9 异常入口为内联汇编，无 `do_el0_svc` 符号可 hook，该功能运行时返回 `-ENOENT`。
- **cntvct monitor 不可用**：4.9 无 `cntvct_read_handler`。
- **arm64_force_sig_fault 退出日志 hook 缺失**（5.8 才有），仅影响异常退出日志记录，进程清理 hook（do_exit/taskstats_exit）正常。
- **`-march=armv8.6-a` 保留未动**：内联汇编需要；SDM845 实际为 v8.2-A，若真机触发 `undefined instruction` 崩溃，需逐条审计新增指令（机器码模板不受影响）。目前属未验证风险。
- 本分支关闭了 `-flto`（GCC 下 LTO 对象无法过 `ld -r`），仅性能差异，无功能影响。

## 补丁清单（相对上游）

1. 头文件守护：4.11 才拆出的 `linux/sched/{signal,task_stack,mm,task}.h`、`uapi/linux/sched/types.h` 全部改为 `__has_include` 守护（8 个文件）。
2. `export_fun.h`：4.9 兼容块——`mmap_*_lock` → `mmap_sem` 系列宏、`p4d` 恒等 shim、`pud_leaf/pmd_leaf` → `pud_huge/pmd_huge`；`get_or_alloc_user_pte` 的 pgd/p4d 分配与 `pte_alloc_one` 参数差异。
3. `lsdriver.c`：`get_user_pages_remote` 增加 4.9 七参分支；`arm64_force_sig_fault` 拆为可选 hook。
4. `arm64_ptedbg-20260822-000439.h`：mprotect 拆公共实现 + `sys_mprotect` 直接参数包装；UDF 入口加 `do_undefinstr` 兜底；hook 安装级联改循环。
5. `virtual_gyro.h` / `virtual_gnss.h`：各加 `sys_sendto` / `sys_ioctl` 4.9 候选符号。
6. `Makefile`：关闭 `-flto`。
