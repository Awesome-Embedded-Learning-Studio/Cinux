# F-ECO:用户生态试金石主线

> 横切主线(立项草案,2026-06-30)。用真实应用当 ABI/系统兼容性试金石,从「自己写自己测」走向「能跑别人的程序」。
> 终极目标:Cinux 能像 Linux 一样平稳使用。
> 状态:**proposal** —— 待审确认后正式进 ROADMAP 横切里程碑表。

## 战略转向:拥抱生态,不造轮子

- OS 的价值在「能让多少别人的东西跑起来」,不在「自己又写了多少东西」。一直待在「自己写给自己测」的舒适区,做出来的是 demo,不是 OS。
- 拥抱生态是**更接近 OS 本质**,不是偏离。这是一贯思路(musl:不自建 libc)的升级——从 libc 扩到整个用户态(init / shell / 网络应用 / 开发工具)。
- **砍 F12 自建轮子**:TinyCC 自举(改用现成 binutils+gcc)、Lua(跑现成二进制)、自建编辑器/包管理。F12 重定位为单一目标:「在 Cinux 上原生跑 gcc/g++,编出 hello.c」。
- 自己造轮子某种程度上是回避真问题(自己出题自己答);拥抱生态才是上考场——能跑别人的二进制 = ABI 兼容性的真考验。

## 试金石梯度(现成软件当试金石,倒推内核缺口)

| 关 | 试金石 | 语言 | 状态 | 定位 |
|----|--------|------|------|------|
| 1 | **busybox** | C | ✅ 已跑通(F-USABILITY 批3/4a + PR#63/64/65/66) | CI 试金石 + syscall 普及主力 |
| 2 | gcc/g++ 原生 | C | ✅ 已跑通(F12-M2 批4-C2 自举 + PR#66 默认 PIE/syscall/perf) | 自举开发者生态 |

每关倒推内核缺口,不造轮子——跑通现成软件本身就是 ABI/syscall 的真检验。

## 第一目标:busybox + syscall 普及

- busybox 全程跑通 ≈ syscall **54 → ~90–100**,达 ROADMAP 底部「syscall 目标 100+」。
- busybox 是 syscall 普及最佳载体:每个 applet 天然针对一组 syscall,跑通的副产品正好把数量推过目标线。

## 准确性原则(防假绿,呼应 F-VERIFY)

F-VERIFY 的教训是「测试太假」(0x1234 假 CoW / 镜像副本 / SMP 空转 / RUN_ALL_TESTS 虚报)。busybox 试金石必须不重蹈:

- **四件套强校验**:每用例 = 输入 + 期望输出(精确比对) + 退出码 + 副作用。例:`cp a b; cat b` 校验 b 内容 == a,不是只看 cp 退出 0。
- **必带负用例**:坏输入(读不存在文件、写只读文件)→ 期望特定 errno/退出码。验准确性,不只 happy path。
- **铁律:退出码 0 不算过,输出 + 副作用精确匹配才算过。**
- 痛点同族:`getdents64` 返 ENOSYS 时 ls 列空但退出 0 = RUN_ALL_TESTS 虚报同族——必须强校验抓。

## 稳定性边界(三划分,不混进 CI 试金石)

用户决策(2026-06-30):

| 层 | 覆盖 | 归属 |
|----|------|------|
| **CI 试金石**(本主线) | 功能稳定 + 并发稳定(-smp) | F-ECO |
| 时间性 soak | 长时间不泄漏 / 不死锁 | **单独解决**(CI 处理不了,用户清楚) |
| 资源边界 / 故障注入 | OOM / fork 炸弹 / 写满盘 | **「从可用到更舒服」新阶段** |

CI 试金石(每 push 跑几分钟)覆盖功能 + 并发;长时间 soak 和资源边界是另外两层,不混进来。

## 独立性(与网络/GUI workflow 并行)

- busybox 批 1–6 全是本地 syscall,不碰 `kernel/net/`、不碰 GUI 子模块,**不抢任何核心文件**。
- 唯一共享:`syscall_nums.hpp` / dispatch 表(三方加号,后合 rebase 秒解)、`main.cpp` 启动段(init 批 6,跟 GUI handoff 不同段)。
- **批 7 socket = F7-M6**:网络 workflow 产 Socket API,busybox 消费——上下游协同,不抢活。

## 跟现有 Feature 的关系

- **不新建内核能力**:F-ECO 是给 F6 / F7 / F10 / F12 已规划内容一个统一验收目标(跑通现成软件)。
- 依赖前置:tmpfs(F6-M4)→ busybox 批 6;Socket(F7-M6)→ busybox 批 7;dup2/fcntl/PTY 闭环(F10-M3 followup)→ busybox 批 4。
- 详见 [00-busybox-touchstone.md](00-busybox-touchstone.md)。

## 文件清单

- [00-busybox-touchstone.md](00-busybox-touchstone.md) —— 第一关:busybox CI 试金石 + 8 批排序 + 用例标准
- (后续)01-gcc-self-host.md —— 第二关:原生 gcc/g++ 自举
