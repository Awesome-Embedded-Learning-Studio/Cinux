# Cinux-Base / Cinux-GUI 子模块并回主仓

> 日期：2026-08-23 · 分支 `faet/migrate_back_third_party`（自 main `4f8f449`）· 状态：✅ 已验证

## 动机

两个子模块当初拆出去的前提已消失：Cinux-Base 定位"可被外部消费的零耦合库"，但无外部消费者、
无独立 CI，且跨库 PR 摩擦早有记录（not_null.hpp 因此落 kernel/lib、RefCount 开发需双仓走）；
Cinux-GUI 为"独立仓并发开发多宿主"而拆，F-GUI-USERSPACE 后 GUI 已是用户态进程、kernel/gui 只剩
event/gui_init/desktop_launch 三根管子，独立开发故事不再成立，反而留下 22 文件 core 源清单三处
手工同步（子仓 CMakeLists / `tools/musl/build-cinux-gui-host.sh` / `cmake/qemu.cmake`）与
pin 先子仓后主仓的 push 顺序负担。用户决策：缩减仓库，并回，不再被外围消费。

## 做法（内容零改动原则）

**原位并回，不搬路径**：两目录留在 `third_party/`，CMake/脚本/`#include` 全部路径不变，
diff 只有 git 元数据。每子模块两步：

1. **导入 commit**：`rm <dir>/.git`（断 gitfile）→ `git rm --cached <dir>`（去 gitlink）→
   `git add <dir>` → 删 `.gitmodules` 对应条目（GUI 步后文件清空删除）。
2. **graft merge**：`git fetch .git/modules/<dir> <pin-SHA>` →
   `git merge -s ours --allow-unrelated-histories FETCH_HEAD` —— 树零变化（`git diff HEAD^1 HEAD`
   为空），仅把子仓提交变为 HEAD 祖先，对象自包含，上游仓归档后仍可 `git log <sha>` 考古。

**关键机械验证**：导入前后逐 blob-SHA 比对
（`git --git-dir=.git/modules/<dir> ls-tree -r <pin>` vs `git ls-files -s`），
Base 80 文件 / GUI 140 文件 mode+SHA 完全一致——逐字节相同、无遗漏无多余，"内容零改动"不是
口头保证而是哈希证明。

提交：Base 导入 `ac69026` + graft `8b8be95`；GUI 导入 + graft（5727b64）。
CI 顺带清理：6 处 `submodules: recursive` 移除（现 no-op）、action 描述同步；
`setup-cinux/action.yml` ccache key 的 `hashFiles('third_party/Cinux-Base')` 保留——现在哈希
真实文件（一次性缓存失效，之后更准）。

文档措辞同步（只改失实现行规则，历史记录不动）：DIRECTIVES A.6「子模块边界」→「库边界」、
删"改动属另一仓库"；CLAUDE.md/AGENTS.md 去掉"子模块"字样；CODING-TASTE 两处例外说明去
"另一仓库"；prompts.md /audit 清单同步；PLAN OPEN GOTCHAS #2 改"在 third_party/（已并回）"；
README GUI 段注明已并回。kernel/test CMake 注释 3 处 submodule→library 字样。

## 验证

- 每次导入后（提交前）：`cmake --build build -j$(nproc)` 全量绿 +
  `timeout 120 ... run-kernel-test-all` 双腿 **953/0** +
  build-rl（CINUX_BUILD_TESTS=ON）`test_host` **66/66**。
- **全新 clone 自包含验证**（`git clone file://` 强制真实对象传输，最接近 GitHub push 后形态）：
  无 `.gitmodules`、零 gitlink、Base 80 + GUI 140 文件齐全、graft 的子仓提交（5a94bed/5727b64）
  在克隆里可解析；CI 同款 configure（`Release + CINUX_BUILD_TESTS=ON`）冷构建两遍绿 +
  `test_host` **66/66** + `run-kernel-test-all` 双腿 **950/0 + musl hello ring-3 smoke
  20/20 PASS**（exit 0，零 FAIL）。
  - 数量差异说明：主树 build/ 的 `CINUX_MUSL_HELLO_SMOKE=OFF`（历史配置）→ main_test 走
    `run_virtio_tests()` 后自写 isa-debug-exit，计 953；克隆按默认 ON → smoke 任务跑完后在
    main_test.cpp:900 自写 isa-debug-exit，QEMU 先出、virtio 不跑，计 950。两形态同内核同源，
    CI 默认即后者（check_test_count baseline 875）。首次冷跑缺 `/hello` 报 ENOENT 是没跑 CI
    前置 `tools/musl/build-musl.sh + build-hello.sh + build-forktest.sh`，补齐即绿，与迁移无关。

## 遗留 / 交接

1. **GUI 陈旧分支独有提交 `c245aa9`**（"fix(core): 输入事件后立即刷新帧"，改 core/pump.{cpp,hpp}）：
   曾在 `.git/modules/third_party/Cinux-GUI` 的陈旧本地 main 上，**不在 pin 5727b64 历史里**
   （patch-id 比对确认未应用）。**已按用户决策丢弃**（2026-08-23 `rm -rf .git/modules`，
   连同 store 一并清除；上游 GitHub 仓亦无此提交，该改动就此废弃）。
2. 上游两仓（github.com/CinuxOS/Cinux-Base、Awesome-Embedded-Learning-Studio/Cinux-GUI）
   建议 archive 只读 + 末条 README 指向本仓路径（用户操作）。
3. 机会性 follow-up（未做，遵守内容零改动）：GUI 22 文件 core 源清单三处合一；
   kernel 仍链接 cinux-gui 但 kernel/ 无文件 include core 头（疑死链，批0 疑点未展开）；
   两子项目自家测试（Base 22 Catch2 / GUI ~20 ctest）主仓 CI 仍不跑，可接进 test_host。

## 后续批：搬迁到 libs/（同日）

用户确认后执行 `third_party/` → `libs/`：`git mv` 两目录 + 编排 CMakeLists
（`third_party/CMakeLists.txt` → `libs/CMakeLists.txt`，改 `CINUX_BASE_DIR` 语义注释），
机械替换 ~30 处构建路径——根 CMakeLists（add_subdirectory + 注释）、kernel/CMakeLists:113
（add_subdirectory → libs）、kernel/mini、kernel/gui 注释、test/CMakeLists 7 处、
cmake/qemu.cmake 的 `CINUX_GUI_HOST_SRCS` 21 行、tools/musl/build-cinux-gui-host.sh 3 处、
setup-cinux action ccache key、CLAUDE/AGENTS/README/DIRECTIVES/QUALITY-GATES/CODING-TASTE
现行路径措辞。源码零改动（include 全路径无关）。`third_party/` 清空删除。
沿用 libs/ext2（F6-M6）先例：libs/ = 本仓自有库的落点。随后追加定名批：`libs/Cinux-Base`→`libs/base`、`libs/Cinux-GUI`→`libs/gui`（用户拍板，gui 与 kernel/gui/、CINUX_GUI、cinux-gui 术语族一致；概念名 Cinux-Base 契约保留文档），同款验证全绿。
验证：全量 build 绿 + 双腿 run-kernel-test-all **953/0** + test_host **66/66** +
`cinux_gui_host` 目标绿（实证 build-cinux-gui-host.sh 新路径）。

## 教训

- **blob-SHA 比对必须用精确 pin SHA**：GUI 子仓 store 的本地 `main` 停在陈旧 c245aa9
  （14 文件时代），先按惯例比 `main` 差点误判"内容不一致"——detached pin 的子模块本地分支
  不可信，一律用 `git submodule status` 的 pin SHA。
- graft 用 `-s ours` 而非 `git subtree add`：subtree add 要求前缀不存在于 HEAD，会逼出
  "先删后加"的破窗中间 commit；`rm .git` + `git add` 原位导入 + `-s ours` graft 两 commit
  每一步树都完整，bisect 安全。
