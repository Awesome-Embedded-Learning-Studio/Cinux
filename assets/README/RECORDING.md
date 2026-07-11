# 📹 Cinux v1.0.0 录像清单

README 截图/录像更新清单。当前 `assets/README/` 下的 4 个 gif + 2 张 png 是教程时代(GUI 用户态化之前)的,要重录展示 v1.0.0 真实能力(SMP / TCP/IP / GUI 用户态 / gcc 自举)。

> Claude 不启动 GUI QEMU(工作流约定),所以录像归你自己。这份清单给每个 gif 的**启动命令 + 录制操作 + 建议时长**,照着录即可。录完放 `assets/README/`,然后小改 `README.md` 的 Screenshots 段引用新文件名(或直接同名覆盖)。

---

## 录制工具

- **Linux**:`peek`(GUI 录屏,导出 gif,推荐)/ `byzanz-record`(命令行)/ `ffmpeg -f x11grab`
- **WSL2**:VNC 窗口跑在 Windows 侧,用 Windows 录屏工具(OBS / ScreenToGif)录后转 gif
- **建议**:gif ≤ 15s、≤ 1MB(README 加载快),分辨率 800×600 或 960×720

## 通用启动

源码构建后,每个 gif 对应一个 `cmake --build build --target <name>` 或 Release 的 `bash run-release.sh`。显示走 VNC `:0` —— `vncviewer localhost:0`。

> 录前确保 `cmake -B build -DCMAKE_BUILD_TYPE=Release -S .` + `cmake --build build -j$(nproc)` 全绿。

---

## 清单(6 个 gif)

### 1. `boot-to-shell.gif` — 启动链(替代 `run_example.gif`)

- **展示**:Bootloader(实模式→长模式)→ Mini Kernel → Big Kernel → Ring 3 → Shell 提示符,全链路。
- **启动**:`cmake --build build --target run-single`(单核,shell fork 稳定)
- **录制操作**:开机到 `~ #` 提示符出现,敲一个 `uname -a`(应报 `Cinux ... 1.0.0 ... Cinux`)。停。
- **建议**:8–12s。

### 2. `gui-desktop.gif` — GUI 用户态桌面(替代 `static_gui.png` + `multi_shell.gif`)

- **展示**:用户态 GUI 进程(Cinux-GUI)桌面 + 多终端窗口 + 拖动。
- **启动**:`cmake --build build --target run`(GUI=ON 默认,-smp 2)
- **录制操作**:桌面起来后,开 2–3 个终端窗口、敲 `ls` / `echo`,拖动一个窗口展示 Z-order/合成。停。
- **建议**:12–15s。

### 3. `gcc-selfhost.gif` — GCC 自举(新增)

- **展示**:Cinux 上 `gcc hello.c && ./a.out` 全闭环(cc1→as→ld→运行)。
- **启动**:`cmake -B build -DCMAKE_ROOTFS_PROFILE=buildroot -DCMAKE_ROOTFS_BUILDROOT_IMG=build/rootfs-gcc.ext2 -DCMAKE_GUI=OFF -S . && cmake --build build --target run-buildroot-usability` —— 或 Release 的 `bash run-release.sh desktop` 进 shell。
- **录制操作**:在 shell 里敲 `gcc hello.c -o /tmp/a.out && /tmp/a.out`,展示输出 `Hello from GCC!`。停。
- **建议**:10–15s(TCG 下 gcc 编译稍慢,可只录编译完成 + 运行那几秒)。

### 4. `smp-dualcore.gif` — SMP 双核(新增)

- **展示**:`-smp 2` 双核 online,两任务真并行。
- **启动**:`cmake --build build --target run-smp`(-smp 2)
- **录制操作**:shell 里跑 `forktest`(或两个并发命令),展示双核调度。串口 log 里 `[SMP]` / per-CPU 输出可佐证。停。
- **建议**:8–12s。

### 5. `network-ping.gif` — TCP/IP(新增)

- **展示**:真 `ping 10.0.2.2`(SLIRP 网关),ICMP reply。
- **启动**:`cmake --build build --target run`(带 e1000 + virtio-net)
- **录制操作**:shell 里敲 `ping 10.0.2.2`,展示 reply 到达。3–4 个 reply 后停。
- **建议**:8–10s。

### 6. `filesystem.gif` — Ext2 文件操作(重录)

- **展示**:ext2 读写(touch/mkdir/ls/cat/rm)+ mount factory。
- **启动**:`cmake --build build --target run-single`
- **录制操作**:`mkdir /tmp/foo && echo hi > /tmp/foo/a && ls /tmp/foo && cat /tmp/foo/a && rm /tmp/foo/a`。停。
- **建议**:10–12s。

---

## 录完接入

把 6 个 gif 放到 `assets/README/`。两种接入方式任选:

- **同名覆盖**(最省事):把新 gif 命名成 `run_example.gif` / `multi_shell.gif` / `parallel_work.gif` / `filesystem.gif` 直接覆盖旧的;新增的 gcc/smp/net 三个用新名,然后小改 `README.md` Screenshots 段加三行 `<img>`。
- **全部新名 + 改 README**:6 个都用语义化新名(上表),录完把 `README.md` 的 Screenshots 段四个 `<img>` 块整体替换。

静态图 `static_gui.png` / `static_cli.png` 可保留(GUI 桌面 + CLI 终端截图仍准确,只是版本旧),或顺手重截。
