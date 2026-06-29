# 【回补】2026-06-27/28 杂项小修 — 键盘 Ctrl 控制字符 + shell "command not found"/run-single

**日期**:2026-06-27(`89fa79e`)/ 2026-06-28(`0438c7b`)　**分支**:feat/f10-tty-dyn
**提交**:`89fa79e`(键盘 Ctrl 控制字符)/ `0438c7b`(shell 127 + run-single)

> 两个小到不值得各开一篇的修复,合并记一篇。都不涉核心路径,但都是 F10
> 用户体验链上的真实缺口。

## 89fa79e — 键盘 Ctrl+字母 → 控制字符

[kernel/drivers/keyboard/keyboard.cpp](../../kernel/drivers/keyboard/keyboard.cpp) 的
`dispatch_key` 收到按键时,**Ctrl 修饰位是一直在跟踪的,但没作用到 ascii 上**——结果
`Ctrl+C` 进来还是 `'c'`,永远到不了 TTY 行规范的 `VINTR` 处理。

修法就是标准 PC 键盘的 `^X = X & 0x1F` 解码:Ctrl 按下且 ascii 是字母时,折成小写判断
(`ascii | 0x20`),落在 `a..z` 就 `ascii &= 0x1F`:

```cpp
if (ctrl && ascii != 0) {
    char lower = static_cast<char>(ascii | 0x20);
    if (lower >= 'a' && lower <= 'z') ascii = static_cast<char>(ascii & 0x1F);
}
```

于是 `^C=0x03` / `^D=0x04` / `^Z=0x1A` / `^\=0x1C` 都能产生,TTY 的 VINTR/VEOF/VSUSP/
VQUIT 才有输入可匹配(F10-M3 批3 的 Ctrl+D EOF、信号投递都依赖这条)。

## 0438c7b — shell 非内置命令失败打印 "command not found" + 加 run-single

两件事:

1. **shell 退出码 127 约定**([user/programs/shell/main.cpp](../../user/programs/shell/main.cpp)):
   原来 `launch_program` 返回负(fork/exec 失败)才打 "command not found"。但子进程里
   `execve` 失败时 child 走 `sys_exit(127)`——这是 shell 的 "command not found" 约定
   (POSIX,127 = 找不到/无法执行)。父 shell 拿到的 `r == 127` 也该当 not-found 处理,
   所以判断从 `r < 0` 改成 `r < 0 || r == 127`。

2. **新增 `run-single` target**([cmake/qemu.cmake](../../cmake/qemu.cmake)):和 `run` 一样
   的设备,但**不开 `-smp 2`**。理由:shell 启动外部程序(type 一个路径)那条 fork 路径
   当时还有 -smp2-only 的跨核 CoW/syscall-frame saga(见
   [fork exit/reap 复活 saga](2026-06-29-smp-fork-reap-resurrection-fix.md) 与
   [gcc-13 #GP](2026-06-29-f10-gcc13-fork-child-setup-gp-fix.md)),单核稳定。
   `run-single` 就是"只想从 shell 跑个外部程序、不想踩 -smp2 saga"的入口;`run`/`run-smp`
   留给 SMP/AP/网络调试。

## 验证

`run-kernel-test-all` 两 leg 绿,无回归(都是用户态/驱动小改,不动内核核心)。`run-single`
启动后 shell 敲路径能稳定 fork+execve 出外部程序(单核)。

## 备注

- `eed2a7d`(docs: A 重写交接 prompt)是**会话交接用的 prompt 文本**,不是工作产物,
  按 document/notes 的定位(发布质量工作记录)不单开笔记——它自解释。
