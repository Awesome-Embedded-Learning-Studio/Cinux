# Init 任务

## 功能概述

这一页我们讲内核怎么把机器交给第一个用户进程。init 在现在的源码里是两个任务的接力，一个叫 kernel_init 的内核任务把根卷挂起来、把 shell 的世界搭好，然后守着一个给 shell 收尸的循环，shell 退了它就再造一个，另一个才是领了 1 号 pid 的用户任务，它跑的是卷上的 `/sbin/init`，也是孤儿收养时全内核唯一认的收养者。init_thread.hpp 的文件头借了 Linux 的说法，把 1 号叫成了 init，把开机的 Main 叫成了 0 号，注释同时交代了借的只是形状，fork、信号和 wait 都不在 init 的地盘上凭空出现，它们跟着需要它们的机制走。身份字怎么发、调度器的名册和切换 sink 长什么样，我们在[进程与调度器](process_and_scheduler.md)里走过，会话与进程组的规则在[任务的身份](pid.md)里讲过了，这一页只讲 init 自己的事。

得交代一下这页的来历。旧版写的是 `kernel/proc/init.cpp` 加 boot task 的叙事，我们核过源码树，那个文件已经不在了，init 的实现搬到了 `kernel/init/init_thread.cpp`，开机的空闲也换成了 Main 自己的循环。所以这一版我们按现在的源码重新搭了叙述。

## init 怎么被造出来和拉起

init 不排在启动序列的表里，Main 亲手把它拉了起来。我们把 `kernel/kernel.cpp` 里 Main 的后半段读一遍，两个调度类都装好了，InstallKernelSwitchSink() 把内核的切换 sink 装上，set_preemption(true) 打开了抢占，然后就轮到了 StartInitThread() 这一行，机器的日常世界从这一行起才算开张。StartInitThread 的注释把时机写得很硬，它必须在 sink 装好了之后才被调，因为这个任务人生的头一件大事就是停进阻塞队列，等卷上的东西搬完，阻塞的全部机械都得活着它才停得下去。造不出来它也不跟你含糊，两处都是硬邦邦的 Check，一台养不起一个任务的机器，没有交得出去的对象。

`kernel/init/init_thread.cpp` 里 StartInitThread 的开头，我们看到的东西是个 lambda。TaskBuilder 拿它当了自己的入口，造出了名叫 kernel_init 的任务，跟着还有一件容易看漏的事，ShellSeats 这个 Meyers 单例（一种全程序只构造一份的写法）负责寄存 shell 的落点，把 prepare_initial_process(*kInit) 的结果记进它 pending 一栏的这一行，在 kernel_init 自己还没入册的时候就把它的孩子也造好了。孩子用的入口是 enter_initial_process，名字被定成了 init，认了 kernel_init 当父亲，seat 的第三个参数传的是 true，也就是源码里的 initial_process，调度器扫了一遍名册，确认了 1 号还空着，就把 pid 1 发到了它头上。这个预造出来的任务停在 kBlocked 里等着，等 kernel_init 把世界搭好了再被叫醒。kernel_init 自己入了册之后，pgid 和 sid 都定成了它自己的 pid，随后的 unblock 把它放跑了起来。

## kernel_init 开工：挂卷与网络探活

kernel_init 的正文我们跟着顺序走。它做的头一件事是 fs::FileWorld::self().init()，把内核唯一的文件系统世界立了起来，file_world.hpp 的注释给它的定位是一张挂载表加一个 ext2 根，AHCI 驱动的块队列在 bring-up 时接上，ext2 的根卷挂到了 `/` 上，挂失败了就是 Check 停机，一个没有根卷的内核没有什么可交代的。接着它把控制台输入的泵 StartConsoleInputPump 起来，让键盘的字有地方流。网络这边我们也没让它闲着，NetWorld 的泵坐不上位就 Check 停机，坐上了之后它还对着 `net::kUserNetworkHost` 打了一轮四个包的 ping 扫描，串口上打印一句应答了几个包、马上要拉 shell 了。这一段我们当探活看就行，应答了几个它都继续往下走，真正拦路的只有挂卷这一关。FileWorld 的挂载清单也值得看全，ext2 的根卷挂到了 `/` 上只是头一笔，跟着 `/dev` 的 devfs、`/proc` 的 procfs 也都在 Check 的看守下挂了上去，块设备这一侧它还把 sata0 坐进了设备模型，NVMe 就绪的时候 nvme0 也跟着坐下，任何一笔挂失败了都是 Check 停机。

## shell 的世界怎么搭

世界是在 build_shell_world 里搭起来的，我们把它当一条流水线看。头一道工序取的是程序，read_volume_file 从挂好的卷上解析出 `/sbin/init`，把至多 64 KiB 的文件读进一块内核持有的缓冲区，读不到时它就把一行 shell 拒绝打印了出来，返回一个空的世界。第二道工序造的是容器，一个崭新的 AddressSpace 和一个崭新的 VmaStore，我们要的就是一个干净的世界，不跟 kernel_init 共享任何的东西。第三道工序把 ELF 装了进去，LoadElfIntoSpace 落好了段，我们顺手读出了 GNU_STACK 的标志（ELF 的栈段标记，内核拿它定栈的权限），把可执行与否记了下来，随后到来的就是三处随机化，堆的起点在页对齐之后加了一个随机的滑动量，mmap 的天花板往下压了另一个随机量，栈顶也从 kUserProgramStackTop 往下滑了一个随机值，三把随机的钥匙都是 arch::RandomWord 出的。

有解释器的话还要装解释器。ReadInterpreterPath 读出 PT_INTERP 里的路径，这是 ELF 的解释器程序头，它告诉内核真正要跑的程序在另一个文件里，LoadInterpreter 把它装进了同一个世界，解释器自己的入口存在的时候，最终交给用户的 entry 就换成了它的入口。AT_RANDOM 是 auxv 里的一项，塞十六个字节的随机数给程序当初始化的种子，这十六个字节是我们现配的，两个随机字拼了进去，连同栈顶一起写进后面的入口图像，图像指的是拼好了入口寄存器和栈之后、可以直接执行的镜像。

栈的工序做得细，我们把它看全。VmaStore 里登记了一段 kStack 的区间，它的长度是 kUserStackBytes 加一个守护页，然后我们从 PMM 真的划出了一个物理页、清了零、用 map_user 映到区间的顶上，权限跟着 GNU_STACK 的标志走。argv 和 envp 的图像落在了这个物理页里，argv 给的是 `{"/sbin/init", nullptr}`，envp 里是 PATH、HOME、TERM 的三条常客，另外还留了一个空位给固件，arch::ReadFirmwareString 从 `opt/cinux/init-env` 里读出了一条 key=value，读到了像样的就追加在后面。落点 kEntryRsp 是我们照着页布局和 kUserEntryStackOffset 算出来的，算好了才交给 WriteEntryPicture，由它把 argv、envp 和落点一并写进了图像，这个 kEntryRsp 就是 shell 醒来时 rsp 的位置。最后一道工序轮到了 MapSignalTrampoline，信号递送要用的蹦床映进了用户世界，映不上整个世界就作废了。

任务那一侧的接驳在 seat_shell_task 里，我们把它的动作排一排。它把 entry、页表根、rsp 三个落点记进了 ShellSeats，一个入座口收齐了全部的落点。世界建好了，它就把预造的 pending 任务取出来用，取不出来它就再造了一个，然后 shell 的 space 和 vmas 接走了世界的所有权，FileWorld 给它的文件表配上了控制台的三条标准流。到这里任务还是停着的，最后一句 proc::Scheduler::self().unblock(*shell) 才把它放进了行里。

## 立户：1 号怎么当上会话首

shell 的身份不用走 setsid 系统调用，内核直接把字写在了字段上。`init_thread.cpp` 里的三行我们并排看，shell 的 pgid 定成了它自己的 pid，sid 也跟着定成了它自己的 pid。控制终端的字段是 controlling_tty，我们看它指到了哪里，答案落在了 driver::ConsoleTty() 的身上。pid 本身是 seat 的 initial_process 发的 1，于是这个任务一身兼了三职，它既是线程组的组长，又是进程组的组长，也还是全场的会话首，控制终端也归了它。紧接着的 fs::AdoptConsoleForeground(shell->pgid) 把控制台的前台进程组交给了这个组，键盘上的字往哪儿送、终端事件按组派给谁，从此都有了答案。我们顺带说清 setpgid 与 setsid 的路是给用户进程搬家用的，开机的这一户是内核代立的门户，[任务的身份](pid.md)里讲的限制管的是搬家，管不到立户的事。

## kernel_init 的等待与重生

立完了户，kernel_init 进的是一个死循环。每一轮它调 seat_shell_task 给世界续一个新的 shell，续不上就把 shell 拒绝打印了出来，整个函数返回了，kernel_init 自己也就退了场。续上了它就调 wait_child 停进等待里，wanted 给的是 -1，也就是任意的孩子，no_hang 是关着的，回执由 receipt 带了回来，shell 退出的时候把父亲叫醒。醒来之后它把回执里的字读出来看，低七位是非零的，说明 shell 是被信号打死的，串口上打印的是信号号，否则打印的是退出码的高八位。打印完循环回到了头，再造一个新的 shell。顺带把一句注释的事说清，init_thread.hpp 的文件头还写着 init 挂完卷、拉起 shell 就结束的话，那是旧模型留下的说法，代码的实况就是我们上面走的这个死循环，shell 死了一个它就补上一个，注释的这句收场我们就不替它圆了，我们读的时候以代码为准。所以我们在 shell 里敲 exit 退出去，机器是不会死的，屏幕上会再给我们一个新鲜的提示符，内核的 shell 是杀不光的，kernel_init 就是那个守夜的人。

## 孤儿收养里的 init

孤儿收养的活由 detach_offspring 承接，它的代码住在 `kernel/proc/scheduler.cpp` 里，我们把它跟 init 的关系捋清。任务退场走的是 exit_current，队列锁里它调了 detach_offspring，后者开头就调了 find_adopter 去找收养者。find_adopter 扫的是全体名册，我们把它的条件读一遍，放行的任务得不是退场者本人，它的 pid 得是 1，space 得持有用户的世界，还有的一条管的是状态，僵尸和死掉的都不收。四条合起来看的话，收养者只能是那个活着的、带着用户世界的 1 号，也就是我们这一页的 shell。kernel_init 自己是不达标的，它是干内核活的任务，手里的 space 是空的，pid 其实也不是 1，所以守夜人收养不了孤儿，它只是收养者的制造者和看守。找到了收养者的时候，每个孩子改认了新的父亲，已经是僵尸的孩子还给收养者补发一个 SIGCHLD，收养者若停在等待里就被捞了起来，孤儿不会变成没人理的死信。找不到收养者的时候，比如 shell 刚死、kernel_init 还没造出下一个的空档里，孩子们改成了无父，僵尸的孙辈直接出册回收，栈还被别的核压着的进了 retired_ 等时机。

## 和 idle（Main 循环）的关系

这个内核没有专门的 idle 任务，这一点我们在[进程与调度器](process_and_scheduler.md)里写过，init 正好是看这件事的好例子。Main 在 StartInitThread 之后其实只剩收尾，EnableSecondaryDispatch() 把 AP 侧的中断分发放开了，然后就进了 cli、run_until_done、EnableIrqAndHalt 的循环，把行里的任务跑干，没人了就开中断停进 hlt。kernel_init 和 shell 在 Main 眼里都是行里的普通任务，被挑起来跑了，跑完了又停回队列，它只是没有活干时的那个等。init_thread.hpp 注释里那句 Boot keeps the CPU 说的就是这件事，借来的 0 号并不对应一个任务，它其实就是 Main 这个循环本身。

## 源码位置

- `kernel/init/init_thread.hpp`、`kernel/init/init_thread.cpp` — StartInitThread、kernel_init 的循环、ShellSeats、build_shell_world、seat_shell_task、enter_initial_process
- `kernel/init/init_sequence.cpp` — 启动序列的步骤表，RunInitSequence 按序跑、失败即停机
- `kernel/kernel.cpp` — Main 里调度类、sink、抢占的装配，StartInitThread 的调用点，cli 与 run_until_done 的 idle 循环
- `kernel/fs/file_world.hpp`、`kernel/fs/file_world.cpp` — 文件系统世界、ext2 根卷的挂载
- `kernel/proc/scheduler.cpp` — exit_current 与 detach_offspring、find_adopter 的收养条件、wait_child
