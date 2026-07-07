# F12: 开发者生态

> 调试工具、Lua 脚本、TinyCC 编译器、编辑器和包管理器。
> 让 Cinux 成为可自举的开发平台。

## 实现决策

**用户决策（2026-07-02）：砍 Lua/TinyCC 自建，走 GCC 自举**——不造轮子，直接让现成 GCC/binutils 在 CinuxOS 跑通并自举编译。真实状态：

- **M1 调试工具**：KALLSYMS ✅（合 F-INFRA/FO，panic backtrace 已用）；GDB stub 内核侧 ⏳ 未做
- **M2 GCC 自举** ✅（2026-07-05 阶段收尾，PR#61/62/66）：cc1→as→ld→./hello 全闭环 + 默认 PIE gcc/g++ + syscall 补全 + perf 降编译 I/O 25%
- ~~M2 Lua 5.4~~ / ~~M3 TinyCC~~：**砍**（01-lua.md / 02-tinycc.md 为砍前历史规划，保留作决策溯源）
- **M3 self-hosting** ⏳ 远期（在 CinuxOS 编 CinuxOS 自己）
- **M4 编辑器 + 包管理器** ⏳ 未立项

## Milestone 依赖（修订后）

```
M1 调试工具（KALLSYMS ✅；GDB stub 待）
M2 GCC 自举 ✅（as+ld+cc1 闭包拷贝，非 Buildroot 编）
M3 self-hosting（远期，依赖更多 syscall + 工具链稳定）
M4 编辑器 + 包管理器（未立项）
```

## 文件清单

| 文件 | Milestone |
|------|-----------|
| [00-debug.md](00-debug.md) | M1: GDB stub + KALLSYMS |
| [01-lua.md](01-lua.md) | M2: Lua 5.4 |
| [02-tinycc.md](02-tinycc.md) | M3: TinyCC 编译器 |
| [03-tools.md](03-tools.md) | M4: 编辑器 + 包管理器 |
