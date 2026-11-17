# 测试与验收

## 功能概述

这一页咱们把 Cinux 的测试与验收从动手的角度完整地走一遍。测试世界本身的构造，host 与 ktest 两个世界怎么共用一份框架、断言的孪生、三类链接与 ktest 的装配，[内核测试基建](kernel/test.md)已经讲完了，机内腿说的就是 ktest 用例在 QEMU 里开机跑出来的成绩，一场用例按 CPU 数拆出来的腿一到两条。一个改动交出去之前要过的检查都在这一页里，咱们会把它们一件件过掉：本地把 host 套件与机内腿跑全的命令，CI 两个 workflow 的编排，注册基线（登记全部测试名的底册）怎么拦回归，红了之后证据去哪里翻。脚本与构建目标在 CMake 侧的形状，检查开关的旗标翻译，[构建脚本](scripts.md)也摆过了。这一页接的是剩下的半件事，什么时候跑什么、跑完看什么，咱们都按现在的源码讲。

今天的框架头是 `test_case.hpp`、`test_assert.hpp` 与 `framework.hpp` 这一批，全部测试的名单收在 `test/ctest-baseline.json` 里，后面数基线的部分咱们把它摊开来数。

## 本地怎么跑

跑测试的门槛是 configure 一遍，咱们敲 `cmake -B build -S .` 就把配置立起来了，编译器与 CMake 版本的门槛[构建脚本](scripts.md)讲过了。配置之后咱们日常用的就是两条命令，`cmake --build build --target test_host` 帮咱们把全部 host 套件跑全，`cmake --build build --target test_kernel` 帮咱们把全部机内腿跑全。host 侧的 ctest 并发认的是环境变量 `CINUX_CHECK_JOBS`，咱们不设的话，`test/CMakeLists.txt` 给的兜底是 2。机内侧的并发不归咱们管，因为它跑的是 `-j 1` 的串行，每条腿要的都是独占的 QEMU，缘由 [内核测试基建](kernel/test.md)摆过了。名单里还有一场名字容易被误会的，`kernel_image` 挂在 ctest 的名单里，验的却是镜像头与尺寸检查自己的机制：`cmake/test/test_kernel_image.cmake` 按 `text_only`、`rodata`、`rodata_data` 三种布局各造一张小镜像，尺寸检查要把合法的三张认下来，再把裁短一截与续长一字的坏件拒掉，跑 `test_host` 的时候它也在场。

单场挑着跑的话，host 套件用它的注册名，咱们敲 `ctest --test-dir build/test -R framework` 就是一场。机内腿的名字是 `kernel_test_名字_cpuN` 的形状，挑某一场用 `-R '^kernel_test_sched_'` 一类的正则。腿的退出码怎么算、串口总结行怎么核，判卷协议 [内核测试基建](kernel/test.md)读过了，这里咱们就不再展开。

要交差的时候咱们跑完整验收。`CINUX_CHECK_JOBS=4 scripts/checks/acceptance.sh` 一条命令就把六格串成一趟：kernel 的 Release 默认格落在 `build`，host 的 asan 与 tsan 两格落在 `build-asan` 与 `build-tsan`，kernel 的 checks 格落在 `build-checks`，末了是 ecosystem 的 none 与 checks 两路。遇到错的时候，格子就当场停了，这是 `run-cell.sh` 开着 `set -e` 的缘故。acceptance 自己倒是不中途退，六格全部走完了它才统一汇总，失败的格子把整段日志贴出来。收尾的汇总行（脚本里的 verdict 行）报给咱们的消息，是四棵构建树加两个生态格全部走通了。日志默认是收在临时目录里的，脚本跑成了就把它们清掉，想让成功的日志也留下来，咱们就设 `CINUX_CHECK_LOG_DIR` 给它指一个目录，失败的日志无论何时都留着，它的路径就写在汇总行里。

本地复现 CI 的完整排法，咱们用的是 `CINUX_CHECK_JOBS=4 bash scripts/checks/matrix.sh`。它一跑起来就把每格的隔离目录造在 `build-matrix` 底下，这个去处咱们可以拿 `CINUX_MATRIX_DIR` 环境变量来改。它铺的格子比验收多几路：host 的 asan 与 tsan，kernel 的 Debug 与 Release 乘 none、ubsan、lockdep 的六路，再补 kernel 的 checks 一路，最后是 ecosystem 的两路。kernel 的每路跑完，matrix 还会拿该路自己烤出来的镜像补一场生态扫描，起的就是 `test_station33_ecosystem.py`。想用编译缓存的话，咱们挂上 `CINUX_COMPILER_LAUNCHER=ccache` 就行，ccache 是编译产物的缓存，输入没变的话它就不重编。selfhost 与 native 两路是不吃这一套的，scripts/checks/README.md 交代了缘由：它们要把构建命令导给 QEMU 里的客人（跑在虚拟机里的那一侧）用，客人拿到的必须是真实的编译器命令。

这些入口之间怎么挑，咱们自己是这么拿的。改的是纯 host 侧的代码，`test_host` 一条就够用了。动了锁、调度或者共享的结构，咱们就加跑一格 kernel 的 checks。大改或者要交差的话，acceptance 给咱们走一趟。怀疑 CI 本身出了问题，matrix 就在本地把同一排法复现一遍。scripts/checks/README.md 还压了几条底线，咱们照着走。QEMU 必须有可用的 `/dev/kvm`（Linux 的虚拟机加速设备），不过纪律归的是验收，README 的原话是不回退 TCG。单场 ktest 的判卷脚本倒是给咱们留了后手：本机摸不到 `/dev/kvm` 时它警告一声，降级 TCG（QEMU 的纯软件模拟后端）把测试跑完。不过验收认的仍是 KVM 环境里的成绩，所以沙箱或者容器拦 KVM 的话，咱们就换允许的环境重跑，没跑成的话要明确记录阻塞项。关闭检查、忽略退出码或者跳过失败用例来骗出验收通过的做法，README 都是明令不许的。操作上咱们还有一条纪律要守，KVM 的用例串行跑，host 的用例可以并行，不同的验收命令之间也要串行，因为镜像和键盘实弹的监控 socket 争用不得。

## 注册基线：谁在名单里

名单的家是 `test/ctest-baseline.json`，咱们把它数了一遍，一共数出了 123 个名字。`kernel_test_` 开头的机内腿占 41 条，剩下的 82 个是 host 一侧的注册项，其中的 81 场是套件，余下的一场 `kernel_image` 验的是上面说过的镜像尺寸检查。41 条腿是 22 个 ktest 镜像拆出来的，19 个镜像跑 CPU1 与 CPU2 的双腿，block、smp、smp_pipe 三个的屏障明确要双核，所以只注册了 CPU2 一条，README 写的正是这个意思。数目是跟着开发走的，咱们引用数目的时候，都以仓库里的名单为准。

守名单的是 `scripts/checks/test-inventory.py`，咱们每格 configure 完，run-cell.sh 就把它调起来了。它拿 `ctest --show-only=json-v1` 从构建目录里读出当前注册的名单，再拿它跟仓库里的基线对，基线里有的名字找不到了，它就在 stderr 上给咱们报 `missing CTest suites`，退出码给的是 1，格子就这样停了。核对通过的话，它给咱们打印一行实数，顺手把名单写进构建目录的 `test-inventory.json` 留档，CI 上传的证据里就有这个文件。今天咱们配置完，它报的是 84 个 host 条目、41 条机内腿，多出来的两场是谁，下一段正好有现成的例子。

规则是不对称的，咱们把不对称讲清。新增是放行的，删除与改名的话，咱们就必须显式更新基线，README 写下的是允许新增、删除与改名须显式更新基线。现成的例子今天就有，`flock` 与 `sched_row` 两场套件是基线更新之后才加进 `test/CMakeLists.txt` 的，scripts/checks/test-inventory.py 照样放行了，84 比 82 多出来的正是这两场。套件内部的断言它不数，README 明说了，基线锁的是注册名单与机内腿，锁不到的地方就交给写用例的人自己把关。

机内腿的成绩判定归 `cmake/test/run_kernel_tests.cmake`，退出码与串口总结行的判卷协议 [内核测试基建](kernel/test.md)读过了，咱们知道退出码 3 才算全过，也知道串口的总结行必须非空，“假绿”这个词就是 scripts/checks/README.md 的原话。咱们在这里补的是它跟 CI 的关系。机内腿挂了的话，咱们能在报错的尾巴上看到整段串口日志，同时 CI 把每条腿自己的 `serial.log` 原样上传，咱们要看的验尸报告就是它，“验尸”的说法出自脚本注释，注释里的原话是 `The serial log is the autopsy report`。

## CI 里怎么跑

CI 的编排住在 `.github/workflows/ci.yml`，push 到 main、对 main 与 develop 的 PR、还有手动触发，都会替咱们把它跑起来。同一个分支或提交（ref）上来了新的提交，GitHub 就把旧的运行取消掉。跑的是五个 job，其中四个管的是测试，一个管的是文档站自己，咱们把每个的差事认一遍。

四个 job 里跑得最快的是 test-infrastructure，超时给了 5 分钟。它干的两件事咱们分开看，一件是拿 `bash -n` 把 acceptance、matrix、run-cell、setup-mke2fs 四个脚本的语法过一遍，另一件跑的是检查基建自己的回归，咱们用的命令是 `python3 -m unittest discover -s test/tools -p 'test_ci_checks.py' -v`。这场自检的路数，咱们看 `test/tools/test_ci_checks.py` 的文件头就明白，它拿合成出来的假 QEMU 进程演练 CI 的失败闸门，文件头自己说了，它不冒领 KVM 的覆盖。README 补了一句，模拟 QEMU 的用例只验证调度与失败处理，真实硬件的覆盖以 KVM 的串口记录为准。

host-tests 是 Release 乘 asan、tsan 的两路，两路跑的都是 `run-cell.sh host`，咱们给它们 25 分钟的超时。上传的步骤挂着 `if: always()`，成了败了都替咱们留证据，host 格传的是 CTest 的 `Testing/Temporary` 目录、`test-inventory.json` 与 `CMakeCache.txt` 三样。

kernel-tests 是 Debug 与 Release 乘 none、ubsan、lockdep 的六路，超时给了 60 分钟。每路跑完 `run-cell.sh kernel` 之后还要追加一场生产内核的实弹，`test_station33_ecosystem.py` 拿该路自己烤出来的镜像把正式内核启动起来，过一遍 BusyBox 与生态基础件的检查。Debug 的 none 路挂了还有一场专门的验尸等着咱们：把 QEMU 按 `qemu-command.txt` 记录的参数拉起来，开上 gdbstub（QEMU 的 GDB 调试桩），gdb 帮咱们批量采样两轮，寄存器、全部线程的栈回溯与 PC 附近的指令写进 `autopsy-gdb.txt`，串口另存的是 `autopsy-serial.log`，步骤名自己写的就是 `Autopsy the frozen guest via QEMU gdbstub`。

ecosystem 是 none 与 checks 的两路，超时给了 60 分钟。checks 这一路要多跑一遍 kernel 的 Release checks，它替咱们把组合检查下的 host 与机内测试也过了一遍，跑完了才在同一个构建树里进生态实弹。生态、网络与 GUI 的冒烟用的都是 `ext2.img` 夹具（测试用的现成盘）加 QEMU 的 snapshot 模式，README 写明了这个边界，日常的开发盘它们碰不到。

selfhost 与 native 的家不在 ci.yml 里，咱们到 `.github/workflows/selfhost.yml` 里找它们。这个 workflow 每周定时给咱们跑一场，排期的 cron 表达式定的是 UTC 周五 19:17，换算到北京时间就是周六的凌晨 03:17，咱们手动触发它也认。selfhost 与 native 各乘 none、checks 的两档，一共排成了四路，每路的超时给了 120 分钟。native 的路跑之前要用 `setup-mke2fs.sh` 装一份 e2fsprogs 1.47.2，它是 ext2 文件系统的用户态工具包，版本是拿 SHA-256 校验值固定的，为什么不用发行版自带的，[构建脚本](scripts.md)讲过了。排期的用意 README 交代了，selfhost 与 native 这样的长任务每周跑一场就够了，本地完整矩阵也不默认把这四路重复一遍。

挂了之后看什么，咱们在 workflow 里数过一遍。三个测试 job 的上传步骤都挂着 `if: always()`，成功失败都替咱们传。kernel 与 ecosystem 的格子把每条机内腿自己的 `serial.log`、`CMakeCache.txt`、`test-inventory.json`、QEMU 的参数文件原样上传，ecosystem 的格子还要加 tcp-smoke 的日志与 `station35/*.ppm` 的 GUI 截图，selfhost 与 native 的格子再加持久化的日志与 native-gui 的截图，GUI 的证据一起交给咱们。红叉的排查路径咱们一般是这么走的，翻 artifact（CI 留档的产物包）里的串口日志，看完之后再决定要不要在本地把同一格复现出来。

格子里的环境由 `.github/actions/setup-cinux` 这个复合动作替咱们准备，它把 GCC 14 工具链与 QEMU、e2fsprogs 这些依赖都装上了，编译缓存 ccache 按格分 key 替咱们存取。KVM 的任务还要多过一道设备检查，咱们要设备在场、可读可写，API 版本也要它拿 ioctl 替咱们验一把，对不上或者缺席的话当场就失败。README 给了一条边界，GitHub 对嵌套虚拟化不提供正式的支持保证，某一次 runner（GitHub 派来跑 job 的机器）的 KVM 能用，咱们不把它当成永久的保证。

## 源码位置

- `scripts/checks/run-cell.sh` — 单格入口，旗标翻译、KVM 检查、基线清点与按套件的构建
- `scripts/checks/acceptance.sh` — 六格验收串与汇总
- `scripts/checks/matrix.sh` — CI 矩阵的本地复现
- `scripts/checks/test-inventory.py` — 基线核对与实数打印
- `test/ctest-baseline.json` — 注册基线
- `scripts/checks/setup-mke2fs.sh` — native 格用的固定版本 e2fsprogs
- `scripts/checks/README.md` — 必跑验收的约定与边界
- `cmake/test/run_kernel_tests.cmake` — 机内腿的 QEMU 判卷
- `cmake/test/test_kernel_image.cmake` — 镜像头与尺寸检查的自测
- `test/CMakeLists.txt` — test_host、test_kernel 目标与机内腿的注册
- `test/tools/test_ci_checks.py` — CI 闸门自检
- `.github/workflows/ci.yml`、`.github/workflows/selfhost.yml` — CI 的编排与周排期
- `.github/actions/setup-cinux/action.yml` — CI 格的环境准备
