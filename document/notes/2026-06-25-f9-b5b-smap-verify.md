# F9 批5b:SMAP/SMEP 真生效验证 + 3 隐藏 bug 修复

> 2026-06-25 · F9 M1 收尾续 · `feat/f9-security`
> test_f9 机制验证(读 EFER/CR4/CPUID)逼出 3 个被 "run-kernel-test 绿" 掩盖的 bug。批5 原 note 说 SMEP/SMAP "代码对、WSL2 验证不了",本批推翻:**SMEP/SMAP 真生效,且已用 -cpu host 验证**。

## 3 个 bug(批3/4 留的)

1. **CPUID.07H 没设子叶 ecx=0**([paging.cpp](../../kernel/arch/x86_64/paging.cpp) enable_smep_smap + test_f9):leaf 7 有子叶,必须 ecx=0 输入才返回主特性(EBX)。原 cpuid 内联汇编只约束 eax,ecx 读垃圾 → EBX=0 → enable_smep_smap 永远以为不支持 → 跳过。改 `"+c"(ecx)`(ecx 作输入+输出,子叶 0)。
2. **test harness([main_test.cpp](../../kernel/test/main_test.cpp))没调 enable_smep_smap**:big_kernel_test 用 main_test.cpp(test harness),不是 production main.cpp。test harness 调了 usermode_init(SCE/NXE 测试过)但没调 enable_smep_smap → test 环境 CR4 从没 SMEP/SMAP。加 enable_smep_smap(对齐 production)。
3. **test_file_mmap 读用户 mmap 页没 stac**([test_file_mmap.cpp:229-231](../../kernel/test/test_file_mmap.cpp#L229)):test 直接 deref 用户页验证文件内容,SMAP 设上后 AC=0 → #PF violation(line 13 注释早预警"F9 后 ring-0 不能直接读用户页",批4 留了)。读前 stac/读后 clac。

## 诊断历程(教训)

- **test_f9 机制验证的价值**:run-kernel-test 931/0 + make run 真程序不崩,**掩盖了** SMEP/SMAP 从没真生效(CPUID 子叶 bug + test harness 没调)。只有读 CR4/CPUID 的 test_f9 才暴露。
- **TEST_ASSERT_TRUE(x & bit) 写法陷阱**:非 0/1 值(0x800)若宏按 ==true 判则假阴性。改 `(x>>n)&1`。
- **别怀疑 QEMU(QEMU 专业)**:-cpu max TCG 写 CR4 SMEP/SMAP 崩,看似 TCG bug;实际是 SMEP/SMAP 真设上后 CinuxOS 违例(test 读用户)。-cpu host(真物理)同崩,坐实是 CinuxOS 违例。debug 找违例点(test_fm_pf 读用户),修 stac。
- **objdump/addr2line 是利器**:enable_smep_smap 符号 + cpuid/cmove 反汇编确认逻辑;addr2line 定位违例 RIP → test_fm_pf::test_pf_reads_file_content。

## 验证

- **-cpu host(真物理 CPU 透传)**:932/0 + test_f9 断言 CR4 SMEP/SMAP 真设上(EFER=0xd01/NXE,CPUID7_EBX 报 SMEP+SMAP,CR4 设)。SMEP/SMAP 真生效且不违例。
- **-cpu max(WSL2 KVM 默认)**:932/0,但 KVM 隐藏 CPUID.07H(EBX=0)→ enable_smep_smap CPUID-gated 正确跳过(SMEP/SMAP 不设,stac/clac NOP 无害)。
- 用户担心的"真机炸":-cpu host 模拟真机,SMEP/SMAP 设上 + 932 全过,真机 OK。

## 验证命令(默认 -cpu max 不验 SMEP/SMAP,用 -cpu host)

```sh
sed -i 's/-accel kvm -cpu max/-accel kvm -cpu host/' cmake/qemu.cmake
cmake -B build -S . && timeout 240 cmake --build build --target run-kernel-test -j$(nproc)
# 验证完改回:sed -i 's/-accel kvm -cpu host/-accel kvm -cpu max/' cmake/qemu.cmake
```

## M1 真正收官

NX(NXE,-cpu max 验证)+ SMEP/SMAP(-cpu host 验证)均真生效。M1 收官(原批5 note 的 "SMEP/SMAP 验证不了" 推翻)。
