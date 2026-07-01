# F-ECO busybox 试金石:真验收 14/14 + kernel/fs 整理

> 2026-07-01。集成线 `feat/f-eco-b2-vfs-syscalls`(PR#59 合 main `d687ea1`)。
> **busybox 1.36(static musl -O2)在 QEMU 真跑通,14 applet 全过**——F-ECO 试金石第一次端到端验证。+ kernel/fs 按性质分子目录整理。

## busybox 真验收 14/14(`7bf1a30`)

首次从"机制测绿"到"真二进制跑通"。流程:build-musl.sh 编 musl 1.2.6 sysroot → 拉 busybox 1.36 源 → musl-gcc static(defconfig + linux/asm/mtd UAPI 头拷进 sysroot 补 musl 缺头;禁 tc/UBI 花哨 applet)→ 进 ext2 盘 → QEMU fork+execve `/bin/busybox <applet>` 看串口真输出 + 退出码。

14 applet 双 leg PASS(真输出):echo / id(`uid=0(root)`) / whoami(`root`) / pwd(`/`) / cat /hello(dump ELF 字节) / wc / uname / true / false(exit 1) / sleep 0 / env / hostname / ls / ps / free(`Mem: 9437184 1092600 8344584`)。

**补的环境件**(全非内核 bug,是盘/测试环境缺):
- `/proc/meminfo`(ProcFS 新伪文件 `ProcMeminfoFileOps`,从 g_pmm 生成 MemTotal/Free/Available/Buffers/Cached/Swap)→ busybox `free` grep 这些 key 出真 PMM 数。
- `/etc/passwd` + `/etc/group`(create_ext2_disk.sh 加)→ whoami/id 解析 uid 0→root。
- 测试内核挂 `/proc`(main_test.cpp 加 `procfs::init()`)→ ps/free 用。

**机制测 vs 真验收的教训**:批2–批8 每批都写了"busybox applet 端到端验收留 CI build",两 leg 绿 = 机制通 ≠ busybox 真能跑。真验收一开始 9/14(5 个因缺 /hello、/proc、/etc/passwd 而 busybox 自身错退,非内核 bug);补齐环境件后 14/14。**真二进制验收是不可替代的一环**。

## kernel/fs 分子目录(`9c27912`)

29 文件平铺 → 核心 VFS(inode/file/vfs_mount/vfs_filesystem/stat/path)留 `kernel/fs/` 根,4 后端各进子目录:`ext2/`(8)、`procfs/`(5)、`devfs/`(3)、`ramdisk/`(3)。纯结构整理(git mv + include 路径批量改),零功能变。`CMakeLists.txt` + `test/CMakeLists.txt` 路径同步。

**GOTCHA**:clang-format 跑全部改动文件时把两个 CMakeLists.txt 当 C 重排(CMake Parse Error)——以后 clang-format 只跑 .cpp/.hpp/.h,别碰 .txt。
