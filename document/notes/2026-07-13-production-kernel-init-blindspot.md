# 生产 kernel_init 测试盲区 — release rc1 首次真 boot 连环 bug

> 日期：2026-07-13 · commits `27b8155` + `db58b2f` + CI 修复 · release v1.0.0-rc1 · 状态：✅ 合 main

## 测试盲区(根因之上的根因)

`run-kernel-test` / `run-kernel-test-all` 跑的是 **`big_kernel_test`**（测试 main，直接跑测试套件），
**不走生产 `kernel_init_thread`**（挂 rootfs → exec init → 桌面/shell）。`cmake --build build --target run`
虽跑生产 big_kernel，但它**总挂 AHCI 控制器**（ahci_test.img 兜底，见 qemu.cmake），所以无-AHCI 路径也从
没被覆盖。**结论：生产启动路径要靠真跑 release 镜像（CI build-images.yml 出的 img）才第一次被测到**——
这正是 rc1 tag 推过、镜像首次 boot 时连环炸的原因。

## release rc1 首次 boot 暴露的生产专属 bug

release `run.sh` 只挂 NVMe rootfs + IDE 启动盘，**不挂 AHCI**（跟 `run` target 不同）。首次 boot：

1. **`AHCI::instance()` 解引用空单例 → #GP**（`ahci.cpp` `s_instance_` 无 AHCI 控制器时是 nullptr，
   `instance()` 返回 `*nullptr`）。`init.cpp` **两处**调用点都没守卫：① rootfs 兜底
   `AHCIBlockDevice::create(instance(), 1)`；② F5-M2 perf 基准 `create(instance(), 0)`。修：`ahci.hpp` 加
   `static bool is_present() { return s_instance_ != nullptr; }`，两处调用前查（`27b8155`）。
2. **NVMe mount 失败原本静默**（`init.cpp` 老代码失败分支无 kprintf）→ 加日志
   `[INIT] NVMe ext2 mount failed: <error> (try AHCI)`，否则只看到落 AHCI 的 #GP，看不到 NVMe 为啥没挂。
3. **desktop variant 的 cmake-flags 原写 `-DCINUX_GUI=OFF`** → 走 `shell_launch`（/bin/sh）不出桌面。
   desktop 该 GUI=ON（`desktop_launch` → `cinux_gui_host`）。

## 串口 buffering 误导崩点

原判断「gcc-13 + GUI=OFF mount 失败」是**错的**——console（gcc-13 + GUI=OFF）mount 其实成功。rc1 崩的
是 `init.cpp` perf 基准那条 `AHCI::instance()` #GP（第二个调用点），串口 buffering 把 mount 成功的日志
吞了，看着像「崩在 Milestone 28 mount」。两处 AHCI 守卫（`27b8155`）一堵全通。**没有 gcc-13 mount
codegen bug**。

教训：串口日志 buffering 会误导崩溃位置——看到的「最后一条日志」未必是崩点，要看 backtrace RIP
addr2line。

## assemble-gcc-rootfs 依赖断链（commit `db58b2f`）

desktop boot 到 GUI 启动后 `/cinux_gui_host` ENOENT：`assemble-gcc-rootfs` 没把 `cinux_gui_host` 列依赖
+ CI musl 步没编它 → assemble 跳过 → rootfs 缺。修：`qemu.cmake` 让 assemble-gcc-rootfs DEPENDS 加
`CINUX_GUI_HOST_ELF`（GUI=ON 时），`cmake --build assemble-gcc-rootfs` 会先编 gui_host（对齐本地 `run` 的
update-rootfs-host 链）。另外 `build-images.yml` 两 variant（console + desktop）统一用 buildroot rootfs
（同一 `rootfs-gcc.ext2`），只差内核 GUI on/off。

## CI apt 包名 gotcha

`build-images.yml` 的 qemu 包名得是 **`qemu-system-x86`**（apt 包名），不是 `qemu-system-x86_64`
（那是装完后的二进制名）。typo 让 setup-cinux apt 步 `E: Unable to locate package qemu-system-x86_64`
exit 100。这个 typo 一直藏着（tag 没推过 + 后续从没真 boot）正是生产路径长期未覆盖的 symptom。

## 收官

两 variant CI（gcc-13）boot 全通：**console**（GUI=OFF）NVMe mount ✓ → shell；**desktop**（GUI=ON）
mount ✓ → ping reply（网络通）→ NVMe perf ✓ → `userspace GUI host launched (pid=2)` 无 ENOENT →
kernel_init 正常退出 → 20s 0 panic。

## 教训

- **改公共启动路径（`init.cpp` `kernel_init_thread`）后，push 前必须真 boot 一次生产镜像**（CI
  build-images 或本地 `run` + buildroot profile），光跑 run-kernel-test 不够——它根本不走这条路径。
- 单例 `instance()` 返回引用前必须有 `is_present()`/`has_*()` 之类的空检查入口；「无硬件时 instance()
  返回 `*nullptr`」是重复踩的坑。
- 串口日志 buffering 会误导崩点，看 backtrace RIP addr2line 别信「最后一条日志」。
