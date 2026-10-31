# Claude Code Instructions for Cinux

本仓处于「**教程 v2 · 重走**」工程:作者带着第一遍的后见之明亲手重搓 Cinux,AI 陪跑。[meta/tutorial-v2-roadmap.md](meta/tutorial-v2-roadmap.md) 只负责两幕卅五站(2026-09-27 由十七站重划,原站 01 一分为六;2026-10-02 站 08 一分为二;2026-10-04 站 08 再对半拆为 08 会疼/09 会听,原 09-22 顺延为 10-23;2026-10-07 站 14 一分为三[ramfs 先行/磁盘驱动与块设备/ext2 与周边],原 15-23 顺延为 17-25;2026-10-16 站 20 一分为三[可观测与调度类/信号与进程组/线程],原 21-25 顺延为 23-27;2026-10-17 站 23 一分为三[跑别人的程序/假文件与真终端/ELF 动态],原 24-27 顺延为 26-29;2026-10-21 站 27 一分为三[驱动基本框架/安全/网络],原 28-29 顺延为 30-31;2026-10-23 站 29 一分为四[校验与回环栈/异步基建与点亮网卡/UDP 与 socket/TCP 完备],原 30-31 顺延为 33-34;同日十划 GUI 与生态调序并插自举站[33 生态与发版/34 编译自己/35 GUI 压轴],卅四站→卅五站)的路线与 §7 进度活表。正文声音来自作者逐字语料，`writing_style.md` 只作成稿审查规约，两者按 `.notes/content-forge.md` 分角色读取。

## 开工三步

1. 读 roadmap §7 找到当前站;2. 开该站材料包(`.notes/v2-packs/pack-NN.md`,没有就先按 roadmap §2 备料);3. 按一站循环干活,每完成一步回填 §7。

## 角色分工(最重要)

- **代码:AI 直接实现+验证,作者 CR**(2026-10-01 作者改令):AI 在主树写码、跑绿(构建/test_host/QEMU/tidy/format)、中文详述提交;Doxygen 注释也由 AI 按房样版式代笔(2026-10-02 作者点名),行内解释性注释仍禁;**不 push 不 PR**——作者过目后自行压缩为一个提交(2026-10-02 作者令)。`code-NN` 工场机制**默认废弃**;`pack-NN` 保留(备料/拍板/坑笔记/实录)。
- **教程文字:AI 起草,作者逐章评审**(四问:学到没/坑透没/状态面板真能跑/是人话吗)。
- **tag = 三齐**(代码站住+章写完+作者验收)才打 `rNN` 前缀 tag;一次一次过,当前站没过不开下一站。

## 执行体系(按需取用,不要一次全读)

| 要什么 | 去哪 |
|---|---|
| 派写手 / 声音走查 / 技术审查 / 连续性走查 / 指代走查 / 深度探测 | `.claude/agents/cinux-{writer,voice-walker,reviewer,thread-walker,reference-walker,probe}.md`(spawn 即自带角色) |
| 教程写作流程与角色边界 | `.notes/content-forge.md`(不保存第二套文风) |
| 架构蓝图(分家/mock/base 内部/编码习语) | `.notes/v2-arch.md` |
| 双仓历史 / 标准数字 | `.notes/retrace/`(数字一律以 07-canonical-numbers.md 为准,别裸考 git) |
| 写手声音 | `.claude/tools/content_forge/tools/humanizer-zh/corpus/VOICE.md` + `approved.toml`;成稿审查 | `.claude/tools/content_forge/writing_style.md` |
| 代码命名规范 | `.clang-tidy` readability-identifier-naming 段(配置即规范,写码前读它,不凭记忆) |

## 硬规矩速记(细则见上表指针)

- **写作**:写手、声音走查、技术审查、连续性走查、指代可解性走查严格隔离,按 `.notes/content-forge.md` 的 Gate 0–5 推进;解释 why;不臆造;批号/SHA/仓库黑话不进正文;历史只在确有证据且能解释当前机制时进入叙事。
- **代码**:产品代码零宏(断言走函数 + `std::source_location` 默认参数;"极端必要"档=测试框架 `TEST()`/`ASSERT_*`);AI 改实际文件**裸改零注释**(注释由作者亲手写,why 只进 Markdown);AI 交付代码前过 `clang-tidy -p build <file>`(配置 2026-09-25 定稿,版本锁定);命名分域:自由函数 `CamelCase`、成员方法 `lower_case`、类型 `CamelCase`、scoped enum `k` 前缀、私有成员 `_` 后缀、namespace 蛇形、全局量 `g_`。
- **QEMU**:一律 `-accel kvm`(硬件穿透,2026-10-01 作者令;`run` 目标已落地),**禁回退 TCG**——软件模拟太慢且行为与真机有偏差;KVM 缺席时大声失败,不许悄悄降级。
- **架构**:顶层目录在需要它的那一站才出现(不预写);kernel 一棵树生长(无 mini 双树);host/kernel 分家=契约一份禁 `#ifdef`、实现两组(kernel 版住 `kernel/` 树按主题归家,先例 `kernel/lib/assert.cpp` ↔ host 版 `test/framework/*_host.cpp`;内核侧实现绝不进 `base/src`,base 只装零 OS 耦合的共用实现)、链接期选择;mock 只住 `test/mock/`(能观测不 stub、能注入不偷换)。**模块语义=config/impl/announce 三件套**(事实旋钮/公告面纯声明/实现全藏 .cpp,最小暴露;2026-10-01 作者立例:PMM 先行)。**私有一律进 .cpp——仅限单模式世界**(kernel=m64);**boot 是独立世界允许抄两遍**(一张二进制混 m16/m32/m64,跨模符号只能 header-inline 每世界免费实例化,.cpp 分体在 boot=跨模静默死转,2026-10-02 实测;boot/early/ 自足拷贝,远景 uni-boot 独立)。**base=OS 无关**(数学/字面量/位图/PhysAddr 这类可住;页大小这类架构事实归 kernel/arch)。**语义轮子优先且双向流动**(2026-10-05 作者立:家底=BitMask/Bitmap/literals/math/PhysAddr/arch page/byte_order/RingQueue;新码先从 base 取,base 没有而新件具通用潜力者第一落点直接是 base——ReadWord32/RingQueue 先例,只有单处私用的才进主题目录;发现存量件有通用潜力则升格入 base;裸位运算、裸乘法尺寸不进新码)。
- **工件**:工作数据一律 `.notes/`(gitignore);每类信息一个家,不设转述层;pack 目录只放活工件,一次性工件归 `.notes/archive/`。

## 最终代码验收必跑（2026-10-07 作者令）

必须运行 `scripts/checks/acceptance.sh`：普通 host/KVM、host ASan+UBSan（含泄漏检查）、独立 host TSan、内核 UBSan+lockdep 的构建与测试。详见 [验收检查说明](scripts/checks/README.md)。分别记录覆盖范围、通过数与限制；检查运行失败不能跳过或关掉开关后宣称通过。涉及 shell 的改动另做相应镜像的 KVM 实弹。代码先查看 base 是否有现成词汇件；有则复用，无而通用则先落 base。

## 旧资产速记

v1.0.0 源码树 + 旧书 book/labs 已下线(旧态在 git `c595922`、`legacy/old_site` 分支、旧 99 tag 与本机 `.notes/materials/` 取材库);meta/ 中 tutorial-v2-roadmap 与 primer-dev-plan 为活规范,三个 v1 迁移文件+replay-state.json 为史料(勿按其"接手入口"行事);ai_prompts 已整体删除(仓库与 .notes 均不留,旧版在 git 历史可查);primer **不冻结**继续;debug-notes/reference/ci/notes 保留复用;源码 CI 已于 2026-10-29 恢复，测试与矩阵入口统一见 scripts/checks/README.md。

## 基建红线

`.notes/materials/` 本地保管不删减不外发;不删旧 tag、不动 `legacy/old_site`;断链无门(CI `ignoreDeadLinks: true`),链接自查。

When in doubt, prefer a smaller verified step over a large speculative one.
