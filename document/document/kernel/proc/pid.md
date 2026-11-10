# 任务的身份：pid、tgid、进程组与会话

## 功能概述

这一页我们把一个任务在内核里的身份是怎么发出来的、又是被谁用掉的，按现在的源码完整走一遍。独立的 PID 分配器已经不在了，旧版写的 `kernel/proc/pid.hpp/cpp` 和 `PID_MAX=256`，我们核过源码树，那批文件已经不在源码树里了，发号的事现在归调度器的入册口 `seat` 管。身份的字段一共四个，它们全长在 Task 的身上：`pid` 是任务自己的号码，`tgid` 是线程组的号码，`pgid` 是进程组的号码，`sid` 是会话的号码，它们的定义都在 `kernel/proc/task.hpp`。它们各自养活了一批系统调用与内核路径，getpid 与 gettid 读的是它，kill 按进程组派信号时靠的是它，wait 挑孩子的时候也用它，procfs 把它抄进了 stat。号码是怎么发的、组与会话的限制在哪里、每条消费的路径怎么走，我们下面一站一站看。

## 号码从哪来：seat 与 pid、tgid

发号的入口只有一个，就是调度器的入册口 `seat`，它的代码在 `kernel/proc/scheduler.cpp`。我们跟着发号的规则走一遍。`initial_process` 为真的时候，它会扫一遍全体名册去确认 1 号还是空着的，随后 seat 把 pid 定成了 1，用户的 init 进程拿的就是这个号，第二次入册的初始进程会被运行时断言当场拦下。其余的任务拿的号都从 `next_pid_` 递增。发号的同时它也把 tgid 定了下来，写的就是 `task.tgid = task.pid` 这一行，也就是说每个任务入册的那一刻都是自己线程组的组长。

clone 的路径会改写线程组的关系。`sys_clone.cpp` 的 HandleClone 照常把孩子 `seat` 进调度器，此时孩子还是自己的组长，接着它读的是 CLONE_THREAD 的标志位，标志在的时候就把孩子的 tgid 重新指回制造者的组，源码里对应的就是 `kChild->tgid = CurrentTask()->tgid` 这一句。头文件的注释把这个约定写得很直白：getpid 回答的是 tgid，所以同一个进程里的所有线程报的都是同一个身份。这不是注释的自说自话，`sys_getpid.cpp` 的实现全文就一行，返回的是 `CurrentTask()->tgid`。`sys_gettid.cpp` 返回的才是 `CurrentTask()->pid`，每个线程在组里都有自己的 tid。线程组的身份还决定了退出的路，`exit_current` 里判的条件是 `kPrev->pid != kPrev->tgid`，线程组成员和无父的任务走的都是直接出册进回收的路，组长的尸体却是留给父亲来收的。线程组怎么退出、父亲怎么等，我们在[进程与调度器](process_and_scheduler.md)里已经走过了，这里只把身份在这些路径里的作用点指出来。

## 进程组与会话：pgid 与 sid 的限制

pgid 和 sid 的来路有三条，我们把三个字段各自的来路一条条讲清。头文件的注释把这些来历记得很全：fork 复制父任务的 pgid 和 sid，`fork.cpp` 里对应的是 `kBorn->pgid = kParent->pgid` 与 `kBorn->sid = kParent->sid` 这两行，clone 造的线程同样从制造者那里继承，setsid 会立一个全新的会话。造孩子的事我们在[进程的复制与替换](fork_exec.md)里已经走过了，这里只看的是身份字怎么传。sid 的初值在被命名之前一直是零，这件事头文件的注释也记了一笔。开机的源头在 `init_thread.cpp`，内核 init 把 shell 的 pgid 和 sid 都定成了它自己的 pid，shell 因此既当上了组长，也当上了会话首，随后它还把控制台的前台进程组交给了这个组。

能改 pgid 和 sid 的系统调用只有两个，它们的实现都集中在 `kernel/syscall/sys_pgrp.cpp`。我们把 HandleSetpgid 的规则完整读一遍。它能动的只有自己，还有自己的孩子，但孩子的一侧有附加的条件，源码写的条件是目标要么就是调用者、要么 `target.parent == kSelf` 且父子在同一个会话里，出了会话的孩子不许动。新组的取值也有讲究：pgid 参数为零的时候新组就是目标自己的 pid，等于让目标自己另立了门户当组长。参数非零的时候要过 `group_allowed` 这一关，它放行的值有两种，一是恰好等于目标的 pid，二是全体名册里确实有人的 pgid 等于这个值，而且那个人跟目标在同一个会话里。这样的设计跟头文件对 sid 的注释是同一句话的两面：一个组只能在它所在的会话内部重新安家，组没有跨会话搬家的通道。查不到这样的组的时候，内核答的就是 EINVAL。

HandleSetsid 管的是立会话，我们把它的门槛和过门槛之后的三件事读一遍。它的门槛查的是调用者当前的处境，`kSelf->pgid == kSelf->pid` 的时候答 EPERM，也就是说你已经是一个组的组长了，就不许再立新的会话，这个限制防的是把已经组好的队伍拆散。过了门槛之后它做的事情有三件，它把 sid 定成了自己的 pid，把 pgid 也跟着定成自己的 pid，再把控制终端清成了空，返回值是新会话的号码。立会话的人从此是会话首，同时带着一个只属于自己的新组。

我们顺带把读的接口也看一眼。getpgrp 直接回答当前任务的 pgid，getpgid 和 getsid 共用的是同一个 `read_identity` 辅助函数，从指名的任务身上读一个身份字出来，pid 参数为零的时候按惯例指调用者自己，查无此人的时候答 ESRCH。

## 身份被谁消费：kill、wait 与 procfs

信号派发是身份字最大的消费者，我们把 kill 的寻址方式一条条看过去。`sys_kill.cpp` 的 HandleKill 把 pid 参数的符号读成了四种寻址的方式，源码的注释把语义写得很清楚：正数指名道姓地发，内核拿这个号走 `with_task` 找到了目标，随后的派发交给 `SendSignal`。零发给调用者自己的进程组。-1 发给全体名册上除自己之外的每个任务，这一扫扫到的是全体名册，bring-up 的 init 也在册、也会被扫到。`sys_kill.cpp` 里那句 init 不在名册上的注释跟这段代码对不上，我们没有替它圆话，照着代码的实况写。小于 -1 的值取绝对值当组号，发给取负之后的那个组。一个信号都没送到的时候答 ESRCH，号码本身不合法的时候在进入组路径之前就答 EINVAL。组派发的本体在 `signals.cpp` 的 `SendToGroup`，它拿 `for_each_task` 扫全体名册、pgid 对上的就发，终端前台派 SIGWINCH 这类事件走的也是它，`pty_node.cpp` 与 `console_node.cpp` 里对前台组的引用都落在这个函数上。组之外我们还能精确到单个线程，线程定向的入口是 tgkill 和 tkill，它们的实现在 `sys_tgkill.cpp`，两个入口共用的是同一个 `signal_thread`，找人用的遍历是 `with_task`，tgkill 还要多验一道 `target.tgid == tgid` 的关卡，防的是拿着别的进程的线程号跨组发信号。

wait 的那一侧也按身份挑人。我们把 wanted 各个取值各挑哪些孩子逐条对上：-1 是任意的孩子，0 是自己进程组里的孩子，小于 -1 是取负之后的那个组，正数是指名道姓的孩子。扫描的后代名册上还有一道共同的门槛，`scan->pid != scan->tgid` 的直接跳过，线程组的成员从来不是 wait 的对象，能被等的只有组长，组匹配用的字就是孩子身上复制来的 pgid。wait4 的入口把这些语义交给 `wait_child`，waitid 走的也是同一个 `wait_child`，只是它认的标志只有 WEXITED 和 WNOHANG，别的组合一律答 EINVAL，这道检查是 `sys_wait.cpp` 里写死的规则。

身份字的最后一个消费者是 procfs，我们看它怎么把号码抄给了用户态。`procfs_init.cpp` 的目录来源用的是 `nth_group_leader`，这个函数按序号走访全体名册、只回答 `pid == tgid` 的任务，所以 `/proc` 根目录下露出来的每个目录都是一个线程组的组长，每个进程都有自己的目录，组里的线程不单独开目录。函数的注释把这个取舍写得很实在：序号是现算的，它不算整表的快照，因为整表拷贝要一块内核栈拿不出的缓冲区，而目录列表本来就是要一次次现走的。`snapshot_by_pid` 把一个任务的 pid、父亲号、pgid、sid 一起拷进 TaskSnapshot，指针是不出锁的，出来的全是值。`procfs.cpp` 的 format_stat 按 Linux 的 stat 排版把前六个字段打了出去，排在最前面的是这些号码。同样的走访口径还被 `sys_sysinfo.cpp` 借去数活着的进程，procs 那一栏数的就是组长。

## 源码位置

- `kernel/proc/task.hpp` — pid、tgid、pgid、sid 四个身份字段的定义与注释，ppid() 的读法
- `kernel/proc/scheduler.cpp` — seat 的发号与 tgid 定值、nth_group_leader、snapshot_by_pid、take_zombie_child、exit_current 的线程组分支
- `kernel/proc/scheduler.hpp` — with_task、for_each_task 的走访接口与 nth_group_leader 的取舍注释
- `kernel/proc/fork.cpp` — fork 对 pgid、sid 的复制
- `kernel/syscall/sys_pgrp.cpp` — setpgid、setsid、getpgrp、getpgid、getsid 的全部规则
- `kernel/syscall/sys_getpid.cpp`、`sys_gettid.cpp`、`sys_getppid.cpp` — 三个读身份的调用
- `kernel/syscall/sys_kill.cpp`、`sys_tgkill.cpp` — 按号码与按组派信号
- `kernel/syscall/sys_wait.cpp` — wait4 与 waitid 的入口，waitid 的标志检查
- `kernel/proc/signals.cpp` — SendToGroup 的组派发
- `kernel/init/init_thread.cpp` — shell 的组与会话立户
- `kernel/fs/procfs/procfs_init.cpp`、`kernel/fs/procfs/procfs.cpp` — procfs 目录来源与 stat 排版
