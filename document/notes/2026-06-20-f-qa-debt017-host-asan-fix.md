# F-QA DEBT-017 修复：host ASAN findings（OOB + 泄漏 + double-free）

> 里程碑：F-QA（质量收敛与加固），Q2 主线前穿插。分支 `feat/f-qa-q2`。2026-06-20。
> 交接文档（`2026-06-20-f-qa-handoff.md`）建议优先修的 production-adjacent bug。Q1-5 给 host 单测开 ASAN（`CINUX_HOST_ASAN`）首跑即抓到，登记为 DEBT-017（P1）。

## 背景

Q1-5 给 host 单测（`test/unit/`，InodeOps/mock 层）开了 ASAN+UBSAN+gcov（`CINUX_HOST_ASAN` option），首跑抓到 4 个 test 失败：`test_pipe`（OOB）、`test_multi_terminal` + `test_fd_table`（泄漏）、`test_sys_pipe`（double-free，复现时新发现——交接文档笼统说「pipe」漏列）。登记 DEBT-017 时**误诊为「`RingBuffer::push_batch` 边界 bug」**，建议碰 Cinux-Base 子模块。

## 诊断（误诊订正）

静态读 `ring_buffer.hpp` `push_batch`：对 `buffer_` 自身**安全**——`tail_ = (tail_+1)%N` 保 [0,N)，`!full()`（`count_<N`）保不溢出。越界是**调用方传错 count**（push_batch 契约信任 `items` 有 `count` 字节，同 memcpy）。**真因不在 push_batch，不在子模块**。

复现（`build-asan` Debug+ASAN）拿完整 ASAN trace，定位 4 处 3 类：

### 1. OOB — `test/unit/test_pipe.cpp:458`
```cpp
int64_t w = pipe.try_write("BBBB", 200);  // 字面量 "BBBB" 只 5B，传 count=200
```
`try_write` → `push_batch("BBBB", min(200, free=96)=96)` 读字面量 96 字节 → 越界 91 字节。ASAN `global-buffer-overflow @ ring_buffer.hpp:73` 的 `items[pushed++]` 侧（非 `buffer_[tail_]`）——`*.LC158`="BBBB" size 5，读其之后。**修**：真实 200B buffer。

### 2. 泄漏 776 — `test/unit/test_fd_table.cpp`（18624B/776 alloc）
全部栈构造 `FDTable table;`。FDTable 是 refcounted heap-only 设计（F3-M2）：生命周期靠 `release()`（refcount→0 close 所有 fd + `delete this`），**无析构**。栈对象**不能调 `release()`**（会 `delete this` 崩）→ 默认析构不释放 alloc 的 File → 泄漏。**修**：`kernel/fs/file.cpp` 加 `~FDTable()` 兜底释放所有未 close 的 File。**对 release 路径幂等**：release 先 close 所有（删 File + 设 nullptr），再 `delete this` → 析构 `delete fds_[i]`（全 nullptr，no-op）。同时强化 production 资源安全不变量（任何忘 release/早返回路径都不泄漏 File）。

### 3. 泄漏 1 — `test/unit/test_multi_terminal.cpp:745`（24104B）
```cpp
uint32_t overflow_id = wm.add_window(new cinux::gui::Terminal(0, 0));  // 满返 0
```
`add_window` 契约：成功接管 win；失败（满/null）返 0 **不接管**（caller 负责）。overflow 的 Terminal 无人释放。**修**：test 持指针，overflow 后 `delete`。

### 4. double-free 9 处 — `test/unit/test_sys_pipe.cpp`
FDTable 析构**暴露**的 test ownership bug。`set()` docstring「ownership transferred to FDTable」，但 test 注释误以为「caller owns, delete manually」（L91/L196），手动 delete FDTable 仍持有的 File。旧设计（无析构）下手动 delete 是唯一释放，故不 double-free（但 FDTable 持 dangling 指针，ASAN 析构不碰故不报）；**加析构后 FDTable 也释放 → double-free**。**修**：删 9 处手动 delete（归 FDTable 析构），保留被 replace 出表的旧 File（docstring「previous File released」，caller 负责）。

## 陷阱

- **误诊代价**：DEBT-017 登记时没复现 ASAN，凭「global-buffer-overflow @ push_batch」表面归因 push_batch。复现拿 stack trace 才发现是 `items[]` 侧（调用方越界），非 `buffer_`。**教训：ASAN 行号只是入口，必须看 stack + 越界对象描述定位真因**。
- **析构暴露隐藏 bug**：加 `~FDTable()` 是正确的资源安全防御，但暴露了 test_sys_pipe 的 ownership 错误（手动 delete FDTable 持有的对象）。这类「析构兜底」改动要 grep 所有手动管理该资源的 test。
- **shell_redirect 侥幸**：`~PipeRedirect` 的 `delete stdin_file` 不 double-free，因构造函数**局部变量 shadow 同名私有成员**（成员恒 nullptr），析构 `delete nullptr` no-op。**消除 shadow 即 double-free**——登记为残留异味，非本债修。
- **CI 对等**：本地 Debug+ASAN 验证不够，CI 是 Release(-O2)+`_FORTIFY_SOURCE=2`+ASAN。flip ci.yml 前必须 Release+ASAN+FORTIFY 对等验证（FORTIFY×ASAN 兼容，实测全绿）。

## 验证

- `build-asan`（Debug+ASAN+UBSAN+gcov）：4 个失败 test 全 ASAN clean；全量 `make test_host` 100%（46 test）。
- `build-asan` 切 Release(-O2)+ASAN+FORTIFY（CI 对等）：全量 `make test_host` 100%（1.00s）。
- 主 build（Release）`run-kernel-test`：**875/0**（FDTable 析构无 production 回归）。
- 编译零警告（F-INFRA 基线保持）。
- **ci.yml host-tests flip `-DCINUX_HOST_ASAN=ON`**：host ASAN 从「known-broken 待修」变硬 CI 门禁。

## production 影响

零。push_batch production（pipe/keyboard）chunk 守护 `min(remain,space)` 安全；FDTable production 经 `release()/close()` 释放（析构幂等 no-op）；sys_pipe production File 归 `current_fd_table()` 不手动 delete。改动：`kernel/fs/file.{hpp,cpp}`（+析构）+ 4 个 host test + ci.yml + 文档。

## 改动清单

- `kernel/fs/file.hpp` / `file.cpp`：加 `~FDTable()` 兜底释放（资源安全不变量）。
- `test/unit/test_pipe.cpp`：try_write 真实 200B buffer。
- `test/unit/test_multi_terminal.cpp`：overflow Terminal 释放。
- `test/unit/test_sys_pipe.cpp`：删 9 处手动 delete（归 FDTable 析构）。
- `.github/workflows/ci.yml`：host-tests flip `-DCINUX_HOST_ASAN=ON`。
- `document/todo/quality/debt.md`：DEBT-017 ✅ + 误诊订正。
