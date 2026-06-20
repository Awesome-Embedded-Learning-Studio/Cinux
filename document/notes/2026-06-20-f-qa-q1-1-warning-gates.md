# F-QA Q1-1 — 零成本编译器警告门禁

> 里程碑：F-QA 质量收敛与加固（Q1 零成本门禁，批 1）。
> 日期：2026-06-20。分支 `feat/f-qa`。基线 main 875/0（F4 SMP 收官 #24）。

## 背景与目标

F-QA Q1 把"已半就位但没收口的零成本编译器门禁"开起来（来源：6-agent workflow 调研 + 对抗 critic）。F-INFRA I-3 已加 `-Wshadow`/`-Wold-style-cast`/`-Wformat=2`/`-Werror=return-type`；本批补上 Linux 风格的零噪声门禁，让「漏 break / 重复分支 / 条件笔误 / VLA / 宏拼写错 / 格式串注入」在编译期 fail，而非靠人眼 review。

## 决策

1. **先 advisory 探命中，再升 `-Werror`**：一次性加全套 + `-Werror` 会炸。先 advisory 编译，按命中决定每个 flag 去留。结果只两类命中：`-Wframe-larger-than` 9 处 + `-Wduplicated-branches` 1 处。
2. **frame-larger-than 撤回本批**：9 处命中是真问题（syscall handler 栈上 `char[PATH_MAX]`，4–8KB/16KB 栈），但修需 path 缓冲改堆（碰 9 个 syscall 路径，设计活），不该塞进门禁批——否则立即 9 警告破坏零警告构建。登记 **DEBT-015**，留专门栈安全批修后再加 `-Wframe-larger-than=1024 -Werror=frame-larger-than`。
3. **duplicated-branches 修**：`pipe.cpp:147` `writer_open_ ? 0 : 0` 两臂都 0（冗余/笔误），简化为 `0`，行为不变（`size_t>=0` 外层三元本就多余）。
4. **7 个零命中 flag 升 `-Werror=`**：`vla`/`implicit-fallthrough`/`undef`/`duplicated-branches`/`duplicated-cond`/`logical-op`/`format-security`。`-Wnull-dereference` 留 advisory（GCC 跨函数分析偶尔假阳）。

## 陷阱

- `-Wimplicit-fallthrough=3` 升 error 后，未来任何 switch 有意穿透必须标 `[[fallthrough]]`（这是要的纪律）。
- `-Werror=format-security` 抓 `kprintf(userstr)` 类格式串注入；当前零命中，未来把用户串当格式串会 fail（正确行为）。
- frame-larger-than 的 9 处**不是门禁误报**，是真栈溢出风险——登记 DEBT-015 不等于忽略，是排到专门批（防"顺手修一大片"引入新不确定性，对齐 QUALITY-GATES 修债纪律）。

## 验证

- `cmake --build build --target big_kernel_test -j$(nproc)`：**0 warning / 0 error**（7 门禁生效 + duplicated-branches 修）。
- `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)`：**875 passed / 0 failed**。

## 产出

- `kernel/CMakeLists.txt`：+7 `-Werror=` 门禁 + `-Wnull-dereference` advisory，注释说明 frame-larger-than 延后理由。
- `kernel/ipc/pipe.cpp`：`duplicated-branches` 简化（行为不变）。
- `document/todo/quality/debt.md`：DEBT-015（栈帧债，P1）。
- 后续：Q1-2 `[[nodiscard]]` 系统化 / Q1-3 CI 多 config 矩阵 / Q1-4 测试项数单调门禁 / Q1-5 host ASAN/TSAN/gcov；DEBT-015 栈安全专门批。
