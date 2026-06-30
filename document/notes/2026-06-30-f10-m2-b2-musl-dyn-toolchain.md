# F10-M2 批2 — musl 动态工具链 + ext2 装 interp

> 2026-06-30。批1 让 kernel 识别动态 ELF;本批把动态 ELF + interp(ldso)
> 造出来并装进 ext2,为批3 端到端 smoke 铺路。分支 `worktree-f10-m2-dynlink`。

## 背景 / 目标

批1 的 kernel PT_INTERP 路径需要一个真实的动态 ELF + 它的 interp 来端到端验证。
本批:① 确认 musl sysroot 自带 libc.so(ldso / shared 默认开)② 新写
`build-hello-dyn.sh` 产非 PIE 动态 hello(ET_EXEC + PT_INTERP=/lib/ld-musl-x86_64.so.1)
③ `create_ext2_disk.sh` + `qemu.cmake` 把 hello-dyn + interp 装进 ext2 镜像。

## 设计 / 决策

- **libc.so 即 interp**:musl `configure --disable-shared [enabled]`(默认开),现有
  `build-musl.sh` 已产 `build/musl-sysroot/lib/libc.so`(822KB),它就是动态链接器 + 共享 libc
  (musl 二合一)。不用改 musl 构建。
- **`build-hello-dyn.sh`**:镜像静态 `build-hello.sh` 的手动 `-nostdlib` 链(musl-gcc wrapper
  在 GCC≥14 坏),但去 `-static` + 加 `-Wl,-dynamic-linker,/lib/ld-musl-x86_64.so.1`。
  `-no-pie` 保证 ET_EXEC(非 PIE),`-lc` 此时解析到 libc.so。crt 仍用 `Scrt1 crti crtbeginS ... crtendS crtn`。
- **interp 装在 PT_INTERP 指定的精确路径** `/lib/ld-musl-x86_64.so.1`:`create_ext2_disk.sh`
  `mkdir lib` + `write libc.so lib/ld-musl-x86_64.so.1`(debugfs write 是拷字节,真文件非 symlink,
  kernel 经 inode->ops->read 读)。hello-dyn 装在 `/hello-dyn`。
- **条件 include**:hello-dyn + interp 同时存在才装(同 /hello 模式),CI 无 sysroot 不受影响。
  `qemu.cmake` 加 `MUSL_HELLO_DYN_ELF`/`MUSL_LDSO_ELF` 变量,穿进 EXT2_IMAGE 与 regenerate-ext2-image 两处调用。

## 陷阱

- **libc.so 在不在的误判**:`ls build/musl-sysroot/lib/libc.so* ld-musl*` 因 `ld-musl*` 无匹配,
  zsh 中止整条命令(哪怕 `libc.so*` 本有匹配)→ 误以为 libc.so 没产。逐项 ls 或加引号确认。
- **host 跑不了动态 hello**:PT_INTERP 是绝对路径 `/lib/ld-musl-x86_64.so.1`,host(glibc)没这文件,
  无 sudo 装不了。故批2 只 readelf 验二进制形态;真跑留批3 QEMU(ext2 上有 interp)。
- **non-PIE 关键**:`-no-pie` 让产物是 ET_EXEC(批1 validate 收 ET_EXEC);若默认 PIE 产物会是
  ET_DYN,主程序就得走 PIE 重定位(follow-up,不在本里程碑)。

## 验证

- `tools/musl/build-hello-dyn.sh` 产 `build/musl/hello-dyn`:`file` 报
  `ELF 64-bit LSB executable, dynamically linked, interpreter /lib/ld-musl-x86_64.so.1`;
  `readelf -hl` 确认 **ET_EXEC + INTERP(/lib/ld-musl-x86_64.so.1)+ PHDR(0x400040)+ DYNAMIC + 4 个 PT_LOAD(R / R+E / R / RW)**,entry 0x401040。形态完全正确。
- `create_ext2_disk.sh` 直接调用(带 artifact)+ CMake `regenerate-ext2-image` 两路都确认
  ext2 有 `/hello-dyn`(15480B)+ `/lib/ld-musl-x86_64.so.1`(822368B)+ `/lib` 目录(debugfs `ls` 确认)。
- 不改 kernel 代码,run-kernel-test-all 不受影响(批1 已验 968/0)。

## 范围栅栏

- 只产工具链 artifact + 装 ext2;端到端跑(QEMU fork+execve /hello-dyn)留批3 的 dyn smoke。
- 不动 musl 构建(shared 默认开已满足);不碰 kernel。

下一步:批3 `CINUX_MUSL_DYN_SMOKE` ring-3 端到端跑 /hello-dyn + 收官。
