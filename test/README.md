# 测试的三层

测试目录照着内核的模块树镜像，一眼能找到对应的地方。

- `unit/<模块>/` — 宿主能力测试。断言的是操作系统做什么（调度、VFS、ext2、
  网络、内存管理、ELF、信号），只许走中立头，不许出现 `kernel/arch/<isa>/`。
  拿 `add_cinux_test(<模块>/<名字>)` 注册，标签是 `<模块>` 与 `capability`。
- `arch/<isa>/` — 架构测试。断言的是 ISA 机制本身（GDT、IDT、TSS、页表项、
  上下文图像、MSR、异常表、ACPI/MADT/HPET/PCI/MSI 布局、PS/2、VESA、boot
  布局）。换端口就按 ISA 重写。拿 `add_cinux_arch_test(<名字>)` 注册，标签是
  `arch`。
- `kernel/<模块>/` — 机内能力测试。同样的能力，跑在真内核里，一个文件一张
  盘一次 QEMU 启动。登记在 `cmake/test/kernel_catalog.cmake`，一条
  `模块/名字:梯级`。setup 可以摸 arch，断言不许依赖 ISA 细节。

`arch_boundary` 这一场扫 `test/unit/`，能力测试里出现 ISA 路径就红。分界靠
这一条门，不靠自觉。

单独跑一组：

```bash
ctest --test-dir build/test -L arch
ctest --test-dir build/test -L capability
ctest --test-dir build/test -R ^kernel_test_
```

## 端口镜像里的自检

armv7 那条车道没有把自检拆成"一文件一 QEMU"，而是把整条端口的自检收在
`kernel/arch/armv7/selfcheck.cpp` 与 `user_mode.cpp` 里，由 `arm_boot` 一次
启动逐条断言。它们算架构测试：断言的是这套端口上的机制能不能用（两级表的
翻译、陷阱帧的往返、时间片能不能换人），换端口要按 ISA 重写。

跑法：

```bash
cmake -S . -B build-armv7 -G Ninja -DCINUX_ARCHITECTURE=armv7
ctest --test-dir build-armv7
```

x86 的能力测试与架构测试不受影响，还是上面那三条命令。
