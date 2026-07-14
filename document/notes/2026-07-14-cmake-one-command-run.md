# 2026-07-14 · cmake 一条命令跑起 desktop+gcc rootfs

## 背景

之前从零跑 desktop+gcc 要手动串 5 步:configure → `build-buildroot.sh` → `build-musl.sh` → `build-cinux-gui-host.sh` → `assemble-gcc-rootfs` → `run`。根因两层断链:

1. **`run` 不依赖 assemble**:buildroot profile 下 `ROOTFS_DEPS=""`(qemu.cmake 老注释「外部产,CMake 不重建」),run 的依赖链里没有 `rootfs-gcc.ext2`,直接挂一个不存在的盘。
2. **assemble 不依赖 buildroot/musl**:`assemble_gcc_rootfs.sh` 假设 `build/buildroot/output/target` 已存在,CMake 故意不跟踪这个依赖(qemu.cmake 老注释「CMake does not track that dependency」)。`cinux_gui_host` 同理不依赖 musl-sysroot。

目标:`cmake -B build -S . && cmake --build build -j16 --target run` 两条命令从零跑起带 gcc 的 desktop。

## 改动

把断链接上。依赖链(**全文件级 DEPENDS**,比 `add_dependencies` 在「target 经 OUTPUT 文件被触达」时可靠):

```
run → rootfs-gcc.ext2 → assemble-gcc-rootfs
                          ├─ cinux_gui_host ELF → musl libc.a
                          └─ buildroot-base stamp
```

- **qemu.cmake**:buildroot profile 下 `ROOTFS_DEPS = rootfs-gcc.ext2`(不再空)。新增 `buildroot-base`(stamp custom_command)+ `musl-sysroot`(libc.a custom_command),用**真实 OUTPUT**(stamp / libc.a)做真增量——无改动时整条链跳过,不是每次 idempotent 重跑。assemble / gui_host 的 custom_command `DEPENDS` 挂文件路径(stamp / libc.a),不挂 target 名。
- **options.cmake**:删 `CINUX_ROOTFS_PROFILE`(生产只 buildroot)。`CINUX_ROOTFS_BUILDROOT_IMG` 保留(默认 `build/rootfs-gcc.ext2`)。
- handcrafted **ext2.img 测试盘保留**(run-kernel-test-all 的 2290 gate + musl SMAP smoke;`EXT2_IMAGE` 独立,不再是生产 profile)。砍 profile 选项不等于砍测试盘。
- `CINUX_BUILDROOT_DIR` 改 cache var(本地默认 `build/buildroot`;CI 传 `build/buildroot-ci` 对齐 setup-cinux 的 cache path)。
- `update-rootfs-host` 加 `${ROOTFS_IMG}` 依赖(它要求 rootfs 已存在,否则 exit 1)。

## 验证

删 `rootfs-gcc.ext2` + buildroot stamp + musl `libc.a`,一条 `cmake --build build --target assemble-gcc-rootfs` 自动拉起全链(EXIT 0,三产物重生):

```
Building buildroot base rootfs → Building musl sysroot → Building userspace GUI host
→ [assemble] staging GCC closure → + /cinux_gui_host → gcc rootfs -> rootfs-gcc.ext2 (132M)
```

## CI 修复(本次改动引入的问题)

初版 `buildroot-base` 写死 `build/buildroot`,但 CI(setup-cinux 的 cache path = `build/buildroot-ci`)显式编 `build/buildroot-ci` → assemble 触发 buildroot-base **全新编一份 `build/buildroot`**(白编 5-10min,可能超 45min timeout)。修:`CINUX_BUILDROOT_DIR` cache var + CI `-DCINUX_BUILDROOT_DIR=build/buildroot-ci` 对齐。顺手删了 CI cmake-flags 里过时的 `-DCINUX_ROOTFS_PROFILE=buildroot`(选项已删)。

## 文件

- `cmake/options.cmake` — 删 `CINUX_ROOTFS_PROFILE`
- `cmake/qemu.cmake` — `ROOTFS_DEPS` 非空 + `buildroot-base`/`musl-sysroot` stamp/libc.a custom_command + assemble/gui_host 文件依赖 + `update-rootfs-host` 排序 + `CINUX_BUILDROOT_DIR` cache var + run-buildroot-usability 注释
- `CMakeLists.txt` — status 行 `ROOTFS_PROFILE` → `ROOTFS`
- `.github/workflows/build-images.yml` + `ci.yml` — cmake-flags:删 profile flag + 加 `CINUX_BUILDROOT_DIR`

## 教训

- buildroot 那套 host 包(host-fakeroot / makedevs / e2fsprogs + 它们的 autotools 构建依赖)是 buildroot 打包 `rootfs.ext2` 的工具链,**不进 rootfs**,只占首次构建时间;砍不掉(fakeroot/e2fsprogs 必需,autotools 是它们的 transitive 依赖)。省时间靠 stamp 增量,不是砍 host 包。
- custom_command `DEPENDS` 挂**文件路径**(stamp / libc.a)比挂 target 名可靠:run 是经 `rootfs-gcc.ext2` 这个 OUTPUT 文件触达 assemble 的,挂 target 名在文件级触发时不一定 fire。
- 改公共构建依赖后,务必查 CI 的路径/缓存是否对齐——本地 `build/buildroot` 和 CI `build/buildroot-ci` 不一致会让 cmake 全新编一份。
