# F-INFRA 批:CI 工程化(ccache + composite action + host-tests 合并)

**日期**:2026-07-03
**里程碑**:F-INFRA(CI 基建清理,横切)
**分支**:`feat/f-usability`(CMake 注册表 + 本批同分支叠)
**commit**:`1e08083`

## 背景

[ci.yml](../../.github/workflows/ci.yml) 5 个 job(host-tests / host-tests-tsan / line-limits / kernel-tests 6-cell 矩阵 / busybox-smoke)覆盖扎实(矩阵 + 缓存 + test-count gate + memory-layout gate),但有 3 处重复/缺失:
1. checkout+install+configure 等 8 个步骤在 4 个 job 间复制粘贴;
2. **无 ccache**——6 个 kernel cell + busybox 各自全量双 build(KALLSYMS),是 CI 总耗时头;
3. `host-tests` 与 `host-tests-tsan` 几乎全等(只差 `-DCINUX_HOST_ASAN` vs `-DCINUX_HOST_TSAN`)。

## 做了什么(ci.yml 净 -76 行)

1. **新增 [.github/actions/setup-cinux/action.yml](../../.github/actions/setup-cinux/action.yml)(composite)**:封装 checkout(submodules)→ apt install(ccache 必装)→ ccache cache → 可选 musl/busybox cache → compiler version → configure(自动加 `-DCMAKE_C/CXX_COMPILER_LAUNCHER=ccache`)→ `Show Cinux configuration`(grep CMakeCache 的 `CINUX_` 镜像进日志)。**build/run/artifact 留 job**(差异最大,不进 composite)。`line-limits` 不用 composite(无 toolchain)。
2. **host-tests 合并**:`host-tests` + `host-tests-tsan` → 单 job 的 `sanitizer: [asan, tsan]` 2-cell 矩阵;memory-layout check `if: matrix.sanitizer == 'asan'`(静态 ELF 分析,sanitizer 无关)。
3. **ccache**:跨所有 job/cell 共享**一个** cache(`key = ccache-${os}-${hashFiles(CMakeLists/toolchain/options/kernel-CMake/Cinux-Base)}`,`restore-keys` 降级 os 级)。ccache 按编译命令 hash 区分 host/freestanding `-mcmodel=kernel`、`-O0`/`-O2`、sanitizer,不会误命中——粗 key 最大化跨 job 复用。build 步骤 `ccache --zero-stats` / `--show-stats` 日志可见命中率。
4. **options.cmake 顺势**:composite 的 `cmake-flags` input 收口所有 `-D` 注入;`Show Cinux configuration` step 把开关状态进 CI 日志(诊断用)。

## 验证

- yaml lint:`yaml.safe_load` 两文件 OK(含 `${{ }}` 表达式)。
- 本地 `cmake -B build-ccache -S . -DCMAKE_BUILD_TESTS=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` configure exit 0,`CMAKE_C/CXX_COMPILER_LAUNCHER=ccache` 入 CMakeCache。
- **ccache 真实命中率留 CI 验证**(本机未装 ccache,sudo 非交互)——CI 重构 inherently 要 CI 跑才能验。

## GOTCHA

1. **composite 不含 build/run**:kernel-tests 要在 configure 后、build 前插 `build-musl`(ext2 镜像要 /hello//forktest),所以 configure(composite 内)在 build-musl 前——功能等价(configure 不读 musl 二进制,build 才读)。
2. **ccache cache key 粗粒度**(只 os + hashFiles,不含 build_type/sanitizer):是有意——ccache 内部按编译命令 hash 区分,粗 key 让所有 cell 共享一个 pool,命中率最高。matrix cell 并发写同 key 时 actions/cache first-writer-wins,其余走 restore-keys,无害。
3. **cmake-flags 表达式**:`${{ matrix.sanitizer == 'asan' && '-DCINUX_HOST_ASAN=ON' || '-DCINUX_HOST_TSAN=ON' }}` 在 yaml unquoted scalar 里含 `${{ }}` 和单引号——`yaml.safe_load` 解析通过(验证过)。

## 下一步

批2:`buildroot-usability` CI job + F-USABILITY 阶段2(sys_cinux_exit + cinux-exit helper + 测试脚本/inittab + CINUX_ROOTFS_PROFILE + run target/CI job)。
