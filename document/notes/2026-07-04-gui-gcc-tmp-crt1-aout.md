# GUI shell gcc 编译闭环:/tmp 悬垂指针 + crt1.o 防御(a.out orphan 续挖)

> 2026-07-04,接 [GUI gcc #GP 兜底](2026-07-04-gui-gcc-gp-force-signal.md)。上篇把 #GP
> livelock 兜住了(force_send),gcc 不再拖死 OS,但编译本身还挂在 `/tmp ccXXXXXX.s`
> 创建。这篇把 `/tmp` 真因挖出来修掉,顺手堵了 crt1.o 的回归,**a.out 的 ext2 orphan
> bug** 是当前阻断,单列续挖。

## 总览(三条 bug,前两条闭环)

| bug | 根因 | 处置 |
|---|---|---|
| `/tmp ccXXXXXX.s` O_EXCL 创建 ENOENT | `vfs_lookup` Parent 模式返回的 `leaf` 指向栈局部 buffer,函数返回即悬垂 → `create()` 拷到垃圾名 → 立刻重 lookup 找不到 | ✅ `af93686` leaf 用稳定数组成员 |
| `ld: cannot use executable file 'crt1.o' as input` | rootfs 里 crt1.o 是 ELF executable(该是 relocatable),staging 对、rootfs 出来时好时坏(没钉死) | ✅ `264738f` assemble 强制 relocatable + sanity check |
| `./a.out` execve 报 file too small 0 bytes | ld 写 a.out 写到了 ino=578,execve lookup 拿到 ino=676(空),同一 `/a.out` 两次 lookup 结果不一致 | 🔴 续挖(ext2 orphan/create/lookup 一致性) |

---

## bug 1:`/tmp` 创建 ENOENT —— vfs_lookup leaf 悬垂指针

### 现象 + 两条错误假设被排除

GUI shell `gcc -fno-pie -no-pie /hello.c`:`cc1` 的 `mkstemp` 开 `/tmp/ccXXXXXX.s`
`flags=0xC2(O_RDWR|O_CREAT|O_EXCL)` 返 `-2(ENOENT)`。但 `/tmp` 已挂(`tmpfs::init`
boot 时跑,GUI/gate 都走),gate `cinux-usability-test.sh` 的 `gcc /hello.c -o /tmp/a.out`
端到端 PASS(记忆 b4a 两 leg 1108/0),证明 **绝对路径 + O_EXCL + /tmp 在内核是工作的**。

静态复核把当时记的两条假设都排了:

1. ❌ `vfs_resolve` 绝对路径没走 tmpfs —— `strncmp("/tmp","/tmp/ccXXX",4)=0` + 边界
   `path[4]='/'` + longest-prefix(4>1)赢 → tmpfs,不是 ext2。
2. ❌ `tmpfs create O_EXCL` 坏 —— `TmpFs::make_node` 只返 `AlreadyExists`/`InvalidArgument`/
   成功,**从不返 NotFound**;且 `do_openat_kernel` 全程不识别 O_EXCL(忽略=当 O_CREAT,
   只会让该 EEXIST 时误成功,产不出 ENOENT)。

`ENOENT` 只可能从 `do_openat_kernel` 的 **kParent 那次 `vfs_lookup`** 来(sys_open.cpp)。
但 gate 能过、GUI 挂 —— 同内核同 rootfs,**环境性 + 实际可复现**。

### 三层仪器钉死(不猜测,加仪器)

OTRACE 已知 `openat('/tmp/ccXXX',0xC2) → -2`。加 deeper trace:

1. `[OPEN]` do_openat_kernel 决策追踪 —— 钉死是 `ret4 (lr2 fail)`:**create ok=1 之后**
   再 `vfs_lookup(kFollow)` 重查刚建的文件,NotFound。
2. `[VLOOK]` vfs_resolve 路由追踪 —— 三次(vfs_lookup)的 `fs` 全相同(`0xFFFFFFFF810FD3A0`
   = 静态 g_tmpfs),`rel` 全一致。**不是路由问题**。
3. `[TMP-C]/[TMP-L]` tmpfs create/lookup —— `create` 存进 node->name 的是
   **`'���'`(垃圾字节)**!节点链进 first_child 了(fc 非空),但 find_child 按真名
   `ccXXX.s` 匹配不到 → NotFound。

### 根因

`vfs_lookup` Parent 模式:
```cpp
LookupResult r;
r.leaf = p;   // p 指向 vfs_lookup 自己栈上的 PathBuf resolved
return r;
```
`resolved` 是 `PathBuf`(heap `new char[PATH_MAX]`),函数返回 PathBuf 析构 `delete[]`,
**leaf 成悬垂指针**。`do_openat_kernel` 拿 `plr.value().leaf` 去 `create()` 拷贝,拷的是
已释放堆残值 → 节点名字乱码 → 重 lookup 真名匹配不到 → ENOENT。

gate gcc-13 能过,纯靠 **栈/堆没被及时覆盖的运气**(create 紧跟 lookup,残值还没被覆盖)。
GUI 调用栈布局不同,残值被覆盖 → 翻车。**「同代码 A 配置过 B 配置挂」= UB 经典签名**,
碰到就该先怀疑内存类 UB,别猜业务逻辑。

### 修复(`af93686`)

`LookupResult` 把 `const char* leaf` 换成 **`char leaf_name[256]` 数组成员**(加
`kLookupNameMax=255` 边界),`vfs_lookup` 用 `memcpy` 把名字拷进去;调用方改 `.leaf_name`。

**关键坑**:不能用「`leaf` 指针指向结构体自己的 buffer」——结构体按值返回时,self-pointer
不会跟着拷到新地址,仍悬垂。**必须用数组成员**,数据随结构体按值拷贝一起走。

笔记:[af93686] `fix(fs): vfs_lookup Parent 模式 leaf 用稳定存储(修悬垂指针)`。

---

## bug 2:crt1.o executable —— assemble 防御

### 现象

`/tmp` 修好后 gcc 进到链接,`ld` 报:
```
/usr/bin/ld: cannot use executable file '.../crt1.o' as input to a link
+ multiple definition of _init / _fini / main / _dso_handle / _TMC_END_ (全指向 crt1.o)
```
正常 crt1.o 是 relocatable(只有 `_start`),不该有这些符号。rootfs 里的 crt1.o 是
**ELF executable(带 PT_INTERP)**。

### staging 对、rootfs 错(没钉死)

- `build/gcc-root/usr/lib/crt1.o`(extract.sh 产物)= **relocatable** ✓
- host `/usr/lib/crt1.o`(extract.sh line 147 cp 源)= **relocatable** ✓
- `build/buildroot/output/target` 里**没有** crt1.o
- 但 `build/rootfs-gcc.ext2` 里的 crt1.o = **executable**(BuildID `c624e0…`)

同脚本同参数:手动 `bash assemble_gcc_rootfs.sh` 出 relocatable,`cmake --build
--target run-single` 触发的 assemble 出 executable。**没钉死根因**(疑似 buildroot target
merge 的非确定性 / stale 缓存)。更糟:`run-single` 不一定重 assemble,会复用 stale
坏 rootfs。

### 修复(`264738f`):防御 + sanity check

不追时序玄学,直接堵:assemble 在 mkfs 前(overlay cp 之后)用 `gcc -print-file-name`
**强制覆盖** crt1.o/Scrt1.o/crti.o/crtn.o 成 gcc 工具链权威 relocatable 版本,再
`file -b` sanity check(必须含 `relocatable`,否则 `exit 1` fail 大声)。

以后这条回归要么被 force 修好、要么 assemble 直接红,**不会再悄悄 ship 坏 rootfs**。

---

## bug 3(🔴 续挖):`./a.out` ext2 orphan / lookup 不一致

### 现象

crt1.o 修好后 gcc 编译 + 链接全通,gcc exit 0,`ls` 看到 a.out。但 `./a.out`:
```
[EXECVE] loading './a.out'
[EXECVE] file too small for ELF header: 0 bytes
[EXECVE] execve result=-5   (EIO)
```
execve 看 a.out 的 `inode->size` = 0。

### 仪器钉死

加 `[EXT2-W]/[EXT2-R]`(ext2 文件读写)+ `[EXT2-WDI]`(write_disk_inode 落盘)+
`[EXT2-CHIT]/[EXT2-CFILL]`(inode cache 命中/miss 重读):

- **ld 写 a.out 写到了 ino=578**:86 次写,i_size 2256→15960,ELF header(off=0,64)
  也写了。`[EXT2-WDI]` 显示 write_disk_inode 落盘到 block=149。
- **create 建的是 ino=676**:`[OPEN] '/a.out' O_CREAT → NotFound` → create 建新 ino=676
  (i_size=0)+ 更新根目录 inode(ino=2)。
- **re-lookup 却挖到 578**:create 后 `get_cached_inode(578)` miss → 从盘读到 disk_i_size=2256
  → ld 的 File 指向 578(写了一堆)。
- **execve lookup 拿到 676**:`[EXT2-CHIT] ino=676 vfs_size=0` → size=0 → ENOENT。

**同一 `/a.out`,ld 的 re-lookup 拿 578,execve 的 lookup 拿 676 —— 两次结果不一致**。
且 578 在 ld 写之前 disk_i_size=2256(不是 0)→ **578 是上一轮残留的 a.out inode**
(orphan:目录项被 rm 了,inode 没回收/数据残在盘上)。

### 下一步

加 ext2 目录项 lookup trace(`ext2 lookup_child: name → ino`),钉死:
- create 加的目录项到底是 `a.out→676` 还是写错了;
- ld re-lookup + execve lookup 各自从目录里读到哪个 ino;
- unlink(`rm a.out`)有没有真把 inode 578 回收(free_inode + bitmap)。

怀疑方向:ext2 create 加目录项时没清掉同名旧条目 / unlink 没回收 inode → orphan
残留 + 目录项查找非确定。这是 ext2 写路径的一致性债(ext2 当年是只读驱动,F6
加的写路径覆盖不全)。

---

## 方法论:抽象 bug(UB)怎么防

这次 bug 1 是教科书般的「UB 调试」案例。复盘后的协议:

1. **先判气质**:症状「同代码 A 配置过 B 配置挂 / 值莫名错乱 / 时好时坏」→ 八成
   UB/内存践踏,进 sanitizer 通道。
2. **找最小可疑函数**,写 5–15 行 host 单测走公共 API 复现它。
3. **`build-asan` 跑**:能复现 → ASAN 报告里根因现成(这次 PathBuf 析构释放 +
   create 又读 = heap-use-after-free,两条栈)。
4. 不能 host 复现(需满内核状态,如 SMP 竞态)→ 才退回手搓 kprintf 仪器。

**内核自己跑不了 ASAN**(freestanding),但 vfs_lookup 这种函数是 host 可编的,
host 单测 + ASAN 本该咬住 —— 现有 host 单测零覆盖 `vfs_lookup(Parent)` 是覆盖洞。
要补的:(a)`scripts/run-host-asan.sh` 一键跑;(b)vfs_lookup(Parent) 哨兵回归测试。

详见 [[dont-ask-whether-to-investigate]] / [[parallel-agents-rigorous-methodology]]。

---

## follow-up 汇总

- **a.out ext2 orphan bug**(🔴 当前阻断):见 bug 3,续挖。
- **cc1 载入卡几十秒**:`elf_load.cpp:96` eager 全读 PT_LOAD 段(cc1 ~50MB → 5 万次
  ext2 块读)。F2 demand-paging 只覆盖 stack/heap,ELF 段还是 eager。修:ELF PT_LOAD
  改 file-backed VMA + 缺页读(跨 F2 + execve + DEBT-023 VMA-inode 引用计数旧坑)。
- **shell 卡顿 + PTY mirror 漏抓**:大概率同根(gui_worker 的 terminal pump 跟不上,
  cc1 重负载下被饿死 → PTY slave buffer 满 → slave_write 短写/返 0 → mirror 的
  `*r>0` 守卫跳过)。mirror miss 不是 kprintf 的问题(kprintf 是同步直打 serial,
  非 ring buffer)。
- **CODING-TASTE 加一条**:「返回的结构体里禁止有指向栈/临时 buffer 的指针字段」——
  引用本案 + vfs_lookup(Parent) 哨兵测试。
- **`scripts/run-host-asan.sh` + vfs_lookup(Parent) host 哨兵**:把 ASAN-first 协议落地。

接 [GUI gcc #GP 兜底](2026-07-04-gui-gcc-gp-force-signal.md)。方法论 [[dont-ask-whether-to-investigate]]。
