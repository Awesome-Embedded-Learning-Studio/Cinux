# /proc/cpuinfo + sched_getaffinity(SMP 可视化 + nproc 报对核数)

> 日期：2026-07-11 · commits `070faeb` + `4680917` · v1.0.0 发版后真实可用化 · 状态：✅ 合 main

## 背景

录 v1.0.0 SMP 视频前补 CPU 核数可视化，顺手治 `LineBuilder` 拼字符串的离谱写法。两个 commit：
`/proc/cpuinfo`（F4 SMP 的 ProcFS 扩展）+ `sched_getaffinity(204)`（让 `nproc` 报对核数）。

## /proc/cpuinfo（commit 070faeb）

枚举 `processor` / `apicid` / `model name` per CPU，`g_acpi_info.cpu_count` 真值。busybox `nproc` 数
`processor` 行报对核数，`cat /proc/cpuinfo` 展示双核拓扑（readdir index 3 + lookup 接线）。

**顺带的三件事**：

1. **C++17 轻量 `format.hpp`**：variadic template + `if constexpr` + type_traits，{fmt} 风格 `{}`
   占位符，类型安全不用 va_arg。替代 LineBuilder 那种 `b.put_s / b.put_u / b.put` 一下一下拼字符串；
   `procfs_content` 4 个 format 全换成 `cinux::fmt::format`，一行一字段。
2. **`version.hpp` 常量一处定义**：`kOsName` / `kOsVersion` / `kOsRelease` / `kCpuModel`，`sys_uname` +
   `/proc/cpuinfo` 复用，不再硬编码散落多处。
3. **`procfs_pseudo.cpp` 拆出**系统伪文件 InodeOps（factory 暴露），`procfs.cpp` 498 行守住 500 上限。

## sched_getaffinity(204)（commit 4680917）

补 syscall 204，返回 CPU 掩码。之前 `nproc` 因 affinity 返回错误而报错核数。实现完整（非 stub）。

## 验证

两 leg run-kernel-test-all TCG 1946/0；`cat /proc/cpuinfo` 双核拓扑可见，`nproc` 报 2。

## 教训

- 用户可见的系统信息（cpuinfo / nproc）是发版后录屏才补的——测试内核只验功能正确，不验「用户看着对」。
- 内核里拼字符串该有类型安全的 format 工具，别靠 LineBuilder 逐字段 append（易错、啰嗦）。
