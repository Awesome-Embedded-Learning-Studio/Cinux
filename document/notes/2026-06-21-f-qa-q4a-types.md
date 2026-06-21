# F-QA Q4a — RefCount + UserPtr 类型先行（2026-06-21）

> F-QA 质量收敛里程碑 Q4 第一批。Q4 = 头号高危债收敛（进程生命周期引用计数洼地）。**Q4a = 类型先行**：引入两个类型不变量（`RefCount` / `UserPtr`）为 Q4b-e 消费者（SharedCwd / FDTable / AddressSpace / Task）铺路。**纯铺路，不碰 fork/execve/PF/validate_user_ptr 行为**。
> 分支 `feat/f-qa-q4`（从干净 main `d708e95` 拉）。2 批 2 commit（主仓）+ 1 子模块 commit。

## 来源

propose 走三重验证后才动手（ultracode workflow）：
1. **spike**：std::atomic 在 kernel freestanding 可用性（决定 RefCount 用 std::atomic 还是 `__atomic_*`）。
2. **联网核验**：Linux `refcount_t` 精确饱和语义（REFCOUNT_SATURATED 值 / inc·dec_and_test 内存序 / 防 UAF 机制）。
3. **对抗审查**：7 lens 审草案（内存序 / drop-in / UserPtr 死代码 / 拆批 / 遗漏 DEBT / freestanding / 范围蔓延）。

## 关键决策（验证后定稿）

### ① RefCount 落 Cinux-Base，用 `__atomic_*` 内建（非 std::atomic）

- **spike 决定性产出**：`std::atomic<uint32_t>` 在 `-ffreestanding -nostdlib` 下**编译过但拖入 libstdc++ 符号 `std::__glibcxx_assert_fail`** → kernel `-nostdlib` 链接必失败。这否决了「对齐子模块 std::atomic 惯例」的初步假设。改用 `__atomic_*` GCC 内建（lock-free inline on x86-64，零外部符号，kernel 与 host test 都可用）。
- 子模块 README 说「`<atomic>` 替代自定义 Atomic」是**理想**，实际 kernel freestanding 不可用（链接 libstdc++）。务实选 `__atomic_*`。

### ② 饱和语义对齐 Linux refcount_t（联网核验订正 2 条）

workflow 联网抓 torvalds/linux `include/linux/refcount.h` 核验，**订正我的 2 条假设**：
- ⚠️ **`REFCOUNT_SATURATED = INT_MIN/2 (0xC0000000)`，非 INT_MIN**。设计意图：离 0 和 INT_MAX **等距**（中点），并发 acquire/release 在 fetch-then-clamp 的非原子窗口内漂移到危险区（0=假释放 / 回绕大正数=假活跃）的概率最低。用 INT_MIN 会丢这层裕度。→ `kRefcountSaturated = 0xC0000000u`。
- ⚠️ **inc 是 `fetch_add_relaxed` 后事后 `set(saturated)` 覆盖**（有意非原子两步，不回滚），非 cmpxchg 回滚。Linux 故意接受 fetch 后的瞬态 unchecked 值，靠 saturated 取中点容忍。
- ✓ dec_and_test CONFIRMED：`fetch_sub_release(1)`；old==1 → acquire fence → return true（到 0，调用者释放）；old<=0 → saturate → return false（防 UAF 下溢）。
- CONFIG_REFCOUNT_FULL 已移除（v5.5），现仅一套严格饱和实现。

实现要点（uint32 适配）：
- `acquire()`：`fetch_add(1, RELAXED)`；`old==0`（add-on-freed UAF）或 `old>=kRefcountSaturated`（已饱和/损坏）→ `store(kRefcountSaturated)`。
- `release()`：`fetch_sub(1, RELEASE)`；old==1 → `thread_fence(ACQUIRE)` + return true；`old==0 || old>=kRefcountSaturated` → saturate + return false。

### ③ UserPtr 入 kernel/lib，纯类型标记（不内置 validate）

- 对齐 Linux sparse `__user`（编译期注解，非运行时方法）。zero-overhead（一指针，无额外状态），参考 `not_null.hpp` 风格。
- 允许 nullptr（用户可传 NULL → syscall -EFAULT），不同于 NotNull（强制非 null）。
- **不内置 validate()**：`validate_user_ptr` 是 `cinux::syscall` 命名空间 inline 函数，且 path_util.hpp 拉入 `kernel/fs/path.hpp` → UserPtr 内置 validate 会引入 **kernel/lib→syscall/fs 反向依赖**。改由调用方 `validate_user_ptr(up.get())`（行为完全不变，DEBT-019 留后续）。
- HONEST LIMITATION：Q4a 仅类型标记，**不增强安全性**（无消费者，类型先行铺路）。

### ④ RefCount 单测放主仓库 test/unit/（BLOCK 纠正）

- 对抗审查 BLOCK 发现：主仓库 `third_party/CMakeLists.txt` **不** `add_subdirectory(Cinux-Base)`（避免 -Werror/-Wshadow flag 泄漏到 big_kernel_common），子模块 `test/` 在主仓构建中**从不执行** → 批1 若把单测放子模块 test/ = 孤儿测试，CI 无人跑（违反 D8 零盲区）。
- 修：`test/unit/test_refcount.cpp`（纳入 test_host，对齐 `test_cinux_base_types` include-only 模式）。子模块只放 header。
- UserPtr 单测放 `kernel/test/test_user_ptr.cpp`（进 run-kernel-test，link kernel 源码）。

## 边界声明（refcount.hpp @file 三条）

1. **服务堆对象生命周期**（Task/AddressSpace/SharedCwd/FDTable），**不服务**物理页 mapcount（DEBT-003 用独立 per-page int16，密度敏感 + 语义不符）。
2. acquire(RELAXED) 不提供对象字段可见性（调用方靠发布者 release store）。
3. 简单生命周期（release-to-zero → 调用者 free），**不支持 on-zero 资源清理 hook**（FDTable 迁移时清理由调用者外移，顺带关 DEBT-010）。

## 拆批 + commit

| 批 | 范围 | Commit |
|----|------|--------|
| 批1 | `refcount.hpp`（Cinux-Base 子模块，`__atomic_*` 饱和语义）+ 主仓 bump + `test/unit/test_refcount.cpp`（10 单测） | 子模块 `e5f6e10` + 主仓 `f8ce80c` |
| 批2 | `user_ptr.hpp`（kernel/lib，zero-overhead 标记）+ `kernel/test/test_user_ptr.cpp`（7 单测）+ main_test/CMake 注册 | 主仓 `50c83bb` |

## 验证

- **test_host** 全绿（refcount 10/10：基本生命周期 + 饱和/下溢/sticky）。
- **run-kernel-test** 875→**882/0**（UserPtr 7/7：default/holds/drop-in/arrow/star/nullptr/const char*；ALL TESTS PASSED）。
- **make run** GUI 实机冒烟：[APIC] switched + [GUI] Desktop（Shell, Calculator）启动到桌面，无 panic（Q4a 生产内核零改动，同 d708e95）。
- 全量 `cmake --build` 绿，零警告。

## 陷阱 / GOTCHA（Q4a 新增可复用）

- **std::atomic 在 kernel freestanding 链接失败**：编译过但拖 `std::__glibcxx_assert_fail`（libstdc++），`-nostdlib` 链接必炸。**通用铁律**：kernel freestanding 原子操作一律 `__atomic_*` 内建（零符号），禁 std::atomic（即便子模块 README 说「允许」）。spike 验证方法：`gcc -ffreestanding -nostdlib -c` 编最小用例 + `nm` 查 undefined 符号。
- **子模块 test/ 不被主仓 CI 构建**：`third_party/CMakeLists.txt` 只 glob include+src，不 add_subdirectory(Cinux-Base)。子模块类型的单测必须放主仓库 test/（host）或 kernel/test（真内核）。子模块 test/ 仅子模块独立 build 时跑（主仓盲区）。
- **REFCOUNT_SATURATED = INT_MIN/2 非 INT_MIN**：等距设计防 race 漂移。任何 refcount 饱和实现照此（联网核验 Linux 源码确认）。
- **UserPtr 不内置 validate**：避免 lib→syscall 反向依赖。类型标记 + 调用方 validate_user_ptr(raw)。

## 下一步

Q4a 类型就绪，Q4b-e 消费：
- **Q4b DEBT-003 CoW mapcount**（独立 per-page int16，**不用 RefCount**）+ DEBT-002 exit cleanup 联动。
- **Q4c DEBT-001 registry 锁 + DEBT-004 waiting_for_child**（原子/去门控）。
- **Q4d DEBT-005 PidAllocator 锁**。
- **Q4e DEBT-002 exit cleanup + DEBT-006 AddressSpace refcount**（RefCount 首个真消费者 + 最险，碰 fork/execve/PF，单独 propose）。

FDTable 迁移（Q4b-e 之一）= 同时关 DEBT-010（guard→atomic 不一致），清理由调用者外移（RefCount 无 on-zero hook）。
