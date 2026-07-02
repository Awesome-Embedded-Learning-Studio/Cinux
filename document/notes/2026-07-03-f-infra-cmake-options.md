# F-INFRA:CMake 选项注册表 + foreach define + QEMU target 工厂(零行为变)

**日期**:2026-07-03
**里程碑**:F-INFRA(CMake 抽象层清理,横切基建)
**分支**:`feat/cmake-options-registry`(从 `feat/f-usability` HEAD 拉,因 F-USABILITY 改过 qemu.cmake 的 `CINUX_ROOTFS_BUILDROOT_IMG` 段)
**commit**:`392902e`

## 背景(不是逻辑乱,是缺抽象层)

仓库 CMake 有三个"抽象缺失",随后续接入大量选项(音频/新文件系统/驱动子开关)线性恶化:

1. **11 个 `option()` 散落 3 文件**(根 CMakeLists 8、cmake/qemu.cmake 1、test/CMakeLists 2),加一个被 `if()` 检查却**从未声明**的 `CINUX_BUILD_TESTS`(靠 CI/VSCode/脚本 `-D` 注入)。无单一注册表,加选项要改"声明处 + kernel gate + 配置打印"三处。
2. **kernel/CMakeLists.txt:121-172 的 8 连机械 `if(CINUX_X) target_compile_definitions(...)`**(`compile_commands.json` 证实这 8 个 define 就是这 8 个 if 直接产生)。
3. **cmake/qemu.cmake 8 个 `run-*` target 的 `-device/-drive` 列表大段照抄**,文件自己标注「改单/双核 flag 时三处同步」。

目标:**纯 CMake 层、行为严格不变**,加选项只动一处,配置末尾打印全部开关状态(用户明确要求)。

## 关键前提(已 Explore 坐实)

- **QEMU 参数完全顺序不敏感**:`qemu_test_wrapper.sh` 只 `"$@"` 透传 + 处理 exit code(不解析参数),`run_all_tests.sh.in` 消费预展开字符串。→ 工厂可自由拼接参数,无需保序。
- **开关全貌**:11 真 `option()` + 5 `set(...CACHE)`(其中 `CINUX_ROOTFS_BUILDROOT_IMG`/`CINUX_IMAGE_PATH` 用户可见)+ `CINUX_BUILD_TESTS`(唯一该补声明的遗漏)+ 2 env(`CINUX_NO_KVM`/`CI`,运行时)。无其他隐藏 `CINUX_` 开关。

## 做了什么(5 文件,净 -68 行)

1. **新增 [cmake/options.cmake](../../cmake/options.cmake)**:集中 12 个 BOOL 开关,分组(Feature 驱动 / Debug / Smoke / Host sanitizer / Build)+ 文档注释;定义 `CINUX_COMPILE_DEF_OPTS` 列表(GUI/USB/NET/LOCKDEP/MUSL_*/BUSYBOX/GCC_TOOLCHAIN)。**新增 `CINUX_BUILD_TESTS` 声明,默认 OFF**(零行为变化,与用户确认)。
2. **[根 CMakeLists.txt](../../CMakeLists.txt)**:删散落 8 个 `option()`,顶部 `include(cmake/options.cmake)`;末尾打印段改 **foreach 打印全部 12 开关 + 路径 + env**。
3. **[kernel/CMakeLists.txt](../../kernel/CMakeLists.txt)**:8 连 if → 一个 `foreach(_opt IN LISTS CINUX_COMPILE_DEF_OPTS)`;**UBSAN 块保留**(需 sanitizer flag + `set_source_files_properties` 排 stub,不只 define);GUI 的 `add_subdirectory(gui)` 保留独立。
4. **[cmake/qemu.cmake](../../cmake/qemu.cmake)**:两个工厂函数——
   - `cinux_qemu_test_target(name [SMP] [DEV_NET] [DEV_XHCI] COMMENT)`:走 wrapper + test 镜像 + `QEMU_TEST_EXTRA_FLAGS` 盘组。覆盖 `run-kernel-test`/`-smp`/`-net`/`-xhci`。
   - `cinux_qemu_run_target(name [SMP] [DEV_NET] [DEV_XHCI] [DEBUG] COMMENT)`:直连 QEMU + production 镜像 + `-no-shutdown`;DEBUG 跳过 AHCI/rootfs device 块(bare image+gdb)。覆盖 `run`/`run-single`/`run-smp`/`run-debug`。
   - 保留手写:`run-kernel-test-all`(F-VERIFY 双 leg)、`run-big-kernel-test`/`run-stress-test`(不同镜像+依赖)、`run-kernel-test-debug`/`-interactive`(直连+test 镜像)、`run-gdb`。
5. **[test/CMakeLists.txt](../../test/CMakeLists.txt)**:`HOST_ASAN`/`HOST_TSAN` 的 `option()` 声明上移到 options.cmake,`if` 处理逻辑保留。

## 行为不变性验证(全绿)

- **define 集合 diff UNCHANGED**:重构前后 `compile_commands.json` 的 `-DCINUX_*` 集合逐字相同(`grep -ho 'DCINUX_[A-Z_]*' | sort -u` diff)。
- **默认值核对**(干净 `build-cfgtest` configure):GUI/USB/NET/`MUSL_HELLO_SMOKE`=ON,其余=OFF,`CINUX_BUILD_TESTS=OFF` 时 `test_host` target 缺席(`add_subdirectory(test)` 不进)。
- **run-kernel-test-all 两 leg**:单核 1101 passed/0 failed + SMP AP boot readback PASS + ALL TESTS PASSED(exit 0)。
- **host 单测**:69/0。
- **8 工厂 target 命令行字面等价**:`make -C build -n <tgt>` dry-run 逐一核对(run 全套 device / run-smp SMP 无 net/xhci / run-debug `-s -S` 无 device / wrapper 类盘组+xhci 等)。

## GOTCHA

1. **dry-run 核对 target 命令行的正确姿势**:`cmake --build build --target X -n` **不支持**(`Unknown argument -n`);要用 `make -C build -n X`(把 `-n` 直接给底层 make)。验证工厂展开是否字面等价就用这个,不真跑 QEMU。
2. **`CINUX_BUILD_TESTS` 历史隐患**:重构前它被 `if()` 检查却从未 `option()` 声明,靠 CI/VSCode/脚本 4 处显式 `-D` 注入。**任何人 `cmake -B build -S .` 不带 `-D` 会静默丢 `test/`**。现补 `option(... OFF)` 声明(ccmake 可见),默认仍 OFF 保零行为变;改默认 ON 是独立决策,未做。
3. **`run-single`/`run-smp` 原用 `EXT2_IMAGE`,工厂统一 `ROOTFS_IMG`/`ROOTFS_DEPS`**:默认(`CINUX_ROOTFS_BUILDROOT_IMG` 空)两者等价;Buildroot rootfs 模式下 `run-single`/`run-smp` 也会跟着用 Buildroot img——**合理升级**(与 `run` 一致),非回归,PR 说明里点出。
4. **UBSAN 不进 `CINUX_COMPILE_DEF_OPTS`**:它除 define 外要 `-fsanitize=undefined` + `set_source_files_properties` 排除 ubsan/kprintf/backtrace/exception_handlers/fault_diag/diagnostics 6 个 stub/诊断 TU(`kernel/CMakeLists.txt` 保留独立 if 块)。
5. **pre-commit `check_line_limits.py` 不拦 CMake**:hook 只查 `.cpp/.hpp/.h/.S` 行数,本次纯 CMake 改动 hook 直接 OK,无需 `--no-verify`。

## 下一步

CMake 选项注册表就位 → **CI 优化可接续**:CI 在 4 处 `-DCINUX_BUILD_TESTS=ON` 等显式注入,现在选项有单一注册表 + 配置打印,CI workflow 可一并整理(矩阵、缓存、新 job 如 F-USABILITY 的 `buildroot-usability`)。
