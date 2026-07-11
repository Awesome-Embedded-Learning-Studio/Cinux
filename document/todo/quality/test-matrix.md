# Cinux — 测试覆盖矩阵（Test Coverage Matrix）

> **F-VERIFY M1 交付物 — 2026-06-27 全子系统坐实（6-agent workflow 审计）。** 平行 [debt.md](debt.md)（代码债）的另一根轴：debt.md 量「代码写得对不对」，本表量「**代码有没有真被测到**」。
>
> **起源**：2026-06-27 audit 抽 165 调试时间坑——19 个根因「基建≠生产」、27 个「机制没真生效」。本表让这两个盲区**可见、可追踪**。47 子系统 × 6 维度 = 282 格，逐格 grep 坐实。

> **图例**：✅真覆盖 / 🟡部分(维度不全,如只 BSP 没 AP) / ⚠️**假测**(镜像副本/哨兵指针/够不到真路径) / ❌缺 / —不适用。

## 覆盖分布总览（ damning 量化）

| 维度 | ✅ | 🟡 | ⚠️假 | ❌缺 | 说明 |
|------|----|----|------|------|------|
| host-unit(纯/mock) | 2 | 3 | 16 | 18 | 12 个是镜像副本(⚠️),改真码不跟着变 |
| host-integration(链接真码) | 5 | 3 | 2 | 37 | 37/47 子系统真码从未被 host 测试链接——镜像/够不到 |
| QEMU-kernel(ring0/BSP) | 23 | 18 | 1 | 5 | 主门有效,大部分 BSP 单核有覆盖 |
| QEMU-SMP | 0 | 0 | 0 | 47 | **47/47 全 ❌(空转)** —— 不 boot_aps,SMP 门名存实亡 |
| ring-3 用户态 | 1 | 14 | 2 | 22 | ~36/47 无/部分;fork CoW 继承 AS 路径死代码 |
| 机制回读 | 1 | 12 | 0 | 18 | ~27/47 无回读;AP 侧零回读(仅 BSP 单点) |

## 矩阵（按子系统簇，每格 = state + 简证；⚠️ 详见下方假测清单）

### 内存管理 (MM)（8 子系统；缺口→M3/M5）

| 子系统 | host-unit | host-integ(真码) | QEMU-kern | QEMU-SMP | ring-3 | 机制回读 | 批 |
|--------|-----------|------------------|-----------|----------|--------|----------|----|
| PMM (Physical Memory Manager, pmm.cpp + mapcount) | ⚠️ test/unit/test_pmm.cpp | ❌ test/CMakeLists.txt 'a | ✅ kernel/test/test_pmm.c | ❌ run-kernel-test-smp is | ❌ no execve/syscall path | ❌ no PMM test reads a CR | M3 |
| Buddy allocator (buddy.cpp) | ❌ no test/unit/test_budd | ❌ no add_cinux_integrati | 🟡 kernel/test/test_buddy | ❌ BSP-only no-op; no AP | ❌ buddy is kernel-intern | — no hw bit (pure index | M3 |
| Slab allocator (slab.cpp) | ❌ no test/unit/test_slab | ❌ slab.cpp never linked | ✅ kernel/test/test_slab. | ❌ BSP-only no-op | ❌ kernel-internal alloca | — no hw bit | M5 |
| VMM / page-table walk (vmm.cpp) | ⚠️ test/unit/test_vmm.cpp | ❌ test/CMakeLists.txt ad | ✅ kernel/test/test_vmm.c | ❌ BSP-only no-op | 🟡 indirect only — Addres | 🟡 test_vmm exercises tra | M4 |
| AddressSpace / VMA (address_space.cpp, vma.cpp) | ⚠️ test/unit/test_address | ❌ add_cinux_test(address | ✅ kernel/test/test_addre | ❌ BSP-only no-op; no cro | 🟡 AddressSpace IS the us | 🟡 address_space.cpp:151 | M5 |
| Page Cache (page_cache.cpp) | ❌ no test/unit/test_page | ❌ page_cache.cpp never l | 🟡 kernel/test/test_page_ | ❌ BSP-only no-op | ✅ kernel/test/test_file_ | — no hw bit | M6 |
| CoW / mapcount (handle_cow_fault in process_new.cpp + fork.cpp/clone.cpp) | ⚠️ test/unit/test_fork_ex | 🟡 add_cinux_integration_ | ⚠️ kernel/test/test_fork_ | ❌ BSP-only no-op; the do | ⚠️ fork/clone tests pass | ❌ no readback of COW PTE | M2 |
| direct-map window (1GB huge pages, DMA phys_to_virt) | ❌ no host unit test for | ❌ dma_pool.cpp not linke | ✅ main_test.cpp:253-266 | ❌ BSP-only no-op | ❌ direct-map is kernel-i | 🟡 main_test.cpp:253-266 | M4 |

### 进程/线程 (proc)（9 子系统；缺口→M2/M3/M5）

| 子系统 | host-unit | host-integ(真码) | QEMU-kern | QEMU-SMP | ring-3 | 机制回读 | 批 |
|--------|-----------|------------------|-----------|----------|--------|----------|----|
| fork/CoW (fork.cpp / sys_fork.cpp) | ⚠️ test/unit/test_fork_ex | ❌ test/CMakeLists.txt:24 | 🟡 kernel/test/test_fork_ | ❌ no fork test drives AP | ⚠️ kernel/test/main_test. | ❌ fork enables no CR/MSR | M3 |
| clone (clone.cpp / sys_clone.cpp) | ❌ no host unit test for | ❌ clone.cpp not in any a | 🟡 kernel/test/test_clone | ❌ no AP-driven clone; SM | ❌ no clone() from a real | ❌ clone writes child->ct | M3 |
| execve / ELF loader (execve.cpp, user_launch.cpp, initial_stack) | ⚠️ test/unit/test_initial | ❌ execve.cpp / user_laun | 🟡 kernel/test/test_fork_ | ❌ no AP execve | 🟡 kernel/test/main_test. | ❌ no readback test provi | M3 |
| scheduler / RoundRobin / context_switch (scheduler.cpp, roundrobin.cpp, context_switch.S) | ⚠️ test/unit/test_schedul | ❌ scheduler.cpp/roundrob | 🟡 kernel/test/test_sched | ❌ no scheduler/migration | — scheduler is kernel-si | ❌ context_switch.S write | M4 |
| signal (signal.cpp / sys_signal.cpp) | ❌ no host unit test for | ❌ signal.cpp not in any | 🟡 kernel/test/test_signa | ❌ no AP signal delivery; | ❌ no real user task rece | ❌ signal enables no CR/M | M5 |
| futex (sync.cpp / sys_futex.cpp) | ❌ no host unit test for | ❌ sys_futex.cpp / sync.c | ✅ kernel/test/test_futex | ❌ futex cross-CPU wake ( | ❌ no user-space pthread | ❌ no hardware bit. No pr | M5 |
| waitpid (sys_waitpid.cpp) | ❌ no host unit test | ❌ sys_waitpid.cpp not in | 🟡 kernel/test/test_fork_ | ❌ no AP waitpid | 🟡 only the opt-in musl s | ❌ no hardware bit; no pr | M5 |
| process group / session (process_group.cpp) | ❌ no host unit test | ❌ process_group.cpp not | 🟡 kernel/test/test_proce | ❌ BSP-only | ❌ no real user task chan | ❌ no hardware bit | M5 |
| TLS (arch/x86_64/tls.cpp, set/get_tls_base) | ❌ no host unit test (TLS | ❌ tls.cpp not host-linka | ✅ kernel/test/test_tls.c | ❌ no AP TLS test; AP MSR | 🟡 clone(CLONE_SETTLS) re | ✅ test_tls.cpp:40 rdmsr | M6 |

### 架构/中断/ABI (arch)（6 子系统；缺口→M3/M4）

| 子系统 | host-unit | host-integ(真码) | QEMU-kern | QEMU-SMP | ring-3 | 机制回读 | 批 |
|--------|-----------|------------------|-----------|----------|--------|----------|----|
| syscall ABI (syscall.S / syscall.cpp dispatch + MSR STAR/LSTAR/SFMASK) | ⚠️ test/unit/test_syscall | ❌ no add_cinux_integrati | 🟡 test/test_syscall.cpp: | ❌ run-kernel-test-smp is | 🟡 musl smoke (main_test. | 🟡 test_usermode.cpp:102- | M2 |
| AP bring-up / MSR (ap_main.cpp, ap_trampoline.S, LSTAR/STAR/SFMASK/EFER.SCE per-CPU) | ❌ no test/unit/* for ap_ | ❌ ap_main.cpp never link | ❌ no run-kernel-test dri | ❌ run-kernel-test-smp bo | ❌ no AP ever runs a user | ❌ AP CR4/MSR/LSTAR/STAR/ | M3 |
| IRQ / interrupt plumbing (irq_handlers.cpp, irq_backend.cpp, irq_init, PIC EOI delivery, vector routing) | ⚠️ test/unit/test_pic.cpp | ❌ pic.cpp / irq_handlers | ✅ test/test_pic_pit.cpp: | ❌ -smp2 BSP-only no-op; | — IRQ plumbing is ring0 | 🟡 test_pic_pit.cpp:78-84 | M3 |
| GDT/IDT (gdt.cpp, idt.cpp, segment selectors, gate type policy, TSS IST/RSP0) | ⚠️ test/unit/test_gdt_idt | ❌ gdt.cpp / idt.cpp neve | ✅ test/test_gdt_idt.cpp: | ❌ -smp2 no-op; per-CPU G | 🟡 GDT user selectors (0x | 🟡 segment registers read | M4 |
| paging CPU-feature bits (SMEP/SMAP/NXE/OSFXSR/OSXMMEXCPT enable + readback) | ❌ no host test for pagin | ❌ paging.cpp never linke | 🟡 enable_smep_smap (pagi | ❌ AP enable_smep_smap (a | 🟡 NXE proven indirectly | 🟡 BSP: CR4 SMEP/SMAP/OSF | M5 |
| usermode ring-transition (usermode.S jump_to_usermode, usermode_init_asm, STAR/EFER.SCE config) | ⚠️ test/unit/test_usermod | ❌ usermode.cpp / usermod | 🟡 test/test_usermode.cpp | ❌ -smp2 no-op; usermode_ | 🟡 ONLY musl smoke (opt-i | 🟡 BSP STAR read back (te | M5 |

### 设备驱动 (drivers)（9 子系统；缺口→M3/M6）

| 子系统 | host-unit | host-integ(真码) | QEMU-kern | QEMU-SMP | ring-3 | 机制回读 | 批 |
|--------|-----------|------------------|-----------|----------|--------|----------|----|
| xHCI/USB controller (bring-up + command pipeline + enumeration) | 🟡 test/unit/test_xhci.cp | 🟡 test/CMakeLists.txt:94 | ✅ kernel/test/test_xhci. | ❌ main_test.cpp never ca | ❌ no user task drives xH | 🟡 test_xhci.cpp:83-90 re | M3 (SMP driver IRQ path) + M4 (mechanism readback of MSI-X/LAPIC) + M2 (DEBT-021 lock add+test) |
| e1000 NIC (RX/TX + SLIRP round-trip) | ❌ no test/unit/test_e100 | ❌ test/CMakeLists.txt ha | ✅ kernel/test/test_e1000 | ❌ run_e1000_tests() at m | ❌ no user task sends/rec | 🟡 test_e1000.cpp:142-147 | M3 (e1000 IRQ/RDTR path) + M5 (production net init+shell ping) |
| AHCI/SATA disk (read/write DMA) | ⚠️ test/unit/test_ahci.cp | ❌ test/CMakeLists.txt:89 | ✅ kernel/test/test_ahci. | ❌ run_ahci_tests()/run_a | ❌ no user task reads dis | ❌ no CR/MSR/register rea | M4 (mechanism readback of GHC.AE/PxCMD.ST) + M3 (AHCI IRQ path) |
| DMA buffer/pool/PRDT | ❌ no test/unit/test_dma_ | ❌ test/CMakeLists.txt li | 🟡 kernel/test/test_dma_p | ❌ run_dma_*_tests() BSP- | — kernel-internal alloca | 🟡 test_dma_pool.cpp:43-4 | M4 (real DMA buffer backing + coherency readback) |
| MSI-X/PCI (capability discovery + table programming + enable) | ⚠️ test/unit/test_msix.cp | 🟡 test/CMakeLists.txt:18 | ❌ NO kernel/test/test_ms | ❌ no msix kernel test at | — PCI/MSI-X is kernel-in | ❌ MsixController::progra | M4 (MSI-X table + MC.Enable readback) + M3 (AP vector program) |
| PCI enumeration (device-class match) | 🟡 test/unit/test_pci.cpp | ❌ test/CMakeLists.txt:90 | ✅ indirectly: test_xhci. | ❌ PCI enumeration runs B | — kernel-internal; no ri | ❌ no test reads a PCI ST | M4 (PCI command-register enable readback) |
| PS/2 keyboard | ⚠️ test/unit/test_keyboar | ❌ test/CMakeLists.txt:82 | ✅ kernel/test/test_keybo | ❌ run_keyboard_tests() B | ❌ no user task reads key | ❌ no CR/MSR/register rea | M2 (link real keyboard.cpp in host test) + M5 (IRQ->ring3 input path) |
| PS/2 mouse + USB HID mouse + USB tablet (absolute) | ⚠️ test/unit/test_mouse.c | ❌ test/CMakeLists.txt:10 | 🟡 kernel/test/test_mouse | ❌ run_mouse_event_tests( | ❌ no user task receives | ❌ no register readback; | M2 (real mouse decode test) + M4 (USB tablet coverage) + M5 (HID->ring3) |
| Net stack (loopback L3 / ARP / dispatch / checksum) [driver-adjacent, not NIC] | ✅ test/unit/test_net_arp | ✅ test/CMakeLists.txt:13 | ✅ kernel/test/test_net.c | ❌ run_net_tests() BSP-on | ❌ sys_ping handler calle | — pure-software stack, n | M5 (ring-3 sys_ping + production net::init) |

### 文件系统/库 (fs/lib)（8 子系统；缺口→M2/M6）

| 子系统 | host-unit | host-integ(真码) | QEMU-kern | QEMU-SMP | ring-3 | 机制回读 | 批 |
|--------|-----------|------------------|-----------|----------|--------|----------|----|
| ext2 (driver: ext2_*.cpp / ext2.hpp) | — test/unit/test_ext2.cp | ⚠️ test/unit/test_ext2_op | ✅ kernel/test/test_ext2. | ❌ run-kernel-test-smp ne | 🟡 CINUX_MUSL_HELLO_SMOKE | — ext2 is pure software | M2 |
| VFS / mount (vfs_mount.cpp, inode.cpp, file.cpp) | — no pure-arithmetic hos | ✅ test/CMakeLists.txt:20 | ✅ kernel/test/test_sysca | ❌ no vfs test runs on a | 🟡 only via CINUX_MUSL_HE | — pure software (lock + | M5 |
| ramdisk (ramdisk.cpp, ustar parser) | — | ⚠️ test/CMakeLists.txt:19 | ✅ kernel/test/test_ramdi | ❌ no ramdisk test on a w | ❌ no real user task read | — pure software (in-memo | M2 |
| pipe (ipc/pipe.cpp, pipe_ops.cpp) | — | ✅ test/CMakeLists.txt:17 | ✅ kernel/test/test_pipe. | ❌ pipe Spinlock + ring b | 🟡 pipe exercised only th | — pure software (Spinloc | M3 |
| kprintf / format (kprintf.cpp, mini/lib/private/format.cpp, vkprintf_impl) | ✅ test/unit/test_kprintf | ✅ test/CMakeLists.txt:17 | ✅ kernel/test/test_kprin | ❌ kprintf/format test su | — kprintf is kernel-inte | — kprintf output goes to | — |
| KALLSYMS / backtrace (lib/kallsyms.cpp, arch/x86_64/backtrace.cpp) | — no host-unit test for | ❌ no add_cinux_integrati | ✅ kernel/test/test_kalls | ❌ kallsyms/backtrace nev | — kallsyms/backtrace are | 🟡 test_backtrace.cpp:32, | M4 |
| panic (kpanic in kprintf.cpp:136, panic() in exception_handlers.cpp:172) / memstats (mm/diagnostics.cpp) | — | ❌ no host test links the | ✅ kernel/test/test_memor | ❌ memstats/panic never r | — panic/memstats are ker | — pure software (kpanic | M4 |
| block device (drivers/ram_block_device, ahci/ahci_block_device, IBlockDevice) | — | ❌ no host test links any | ✅ kernel/test/test_block | ❌ block-device tests run | 🟡 real AHCI block device | — block device is MMIO/D | M5 |

### 安全/用户边界 (sec/user)（7 子系统；缺口→M4）

| 子系统 | host-unit | host-integ(真码) | QEMU-kern | QEMU-SMP | ring-3 | 机制回读 | 批 |
|--------|-----------|------------------|-----------|----------|--------|----------|----|
| ASLR offset helpers (stack/mmap/brk) | ❌ no host test; aslr.hpp | ❌ not linked by any host | 🟡 kernel/test/test_aslr. | ❌ run-kernel-test-smp is | ❌ no ring-3 test reads r | ❌ pure-software PRNG mas | M3 |
| Credentials (uid/gid/euid/egid, sys_set*/get*) | ❌ no host test; no test/ | ❌ sys_creds.cpp never li | 🟡 kernel/test/test_creds | ❌ BSP-only no-op; creds | ❌ no ring-3 task perform | — pure-software credenti | M4 |
| Stack canary (SSP / __stack_chk) | ❌ no host test | ❌ crt_stub.cpp never lin | ❌ NO kernel test reads _ | ❌ no-op; canary never te | ❌ no ring-3 test corrupt | ❌ CRITICAL GAP: __stack_ | M2 |
| user_ptr copy (UserPtr<T> marker + validate_user_ptr) | ❌ UserPtr is header-only | ❌ user_ptr.hpp never lin | 🟡 kernel/test/test_user_ | ❌ no-op; never tested AP | ❌ no ring-3 test passes | — pure-software marker, | M4 |
| Initial stack / auxv builder | 🟡 test/unit/test_initial | ❌ not an integration tes | ❌ no kernel-side test re | ❌ no-op | 🟡 musl smoke (main_test. | — pure-software buffer l | M5 |
| fd table (FDTable alloc/close/get) | — n/a (this IS the host | ✅ test/CMakeLists.txt:19 | ❌ no kernel-side FDTable | ❌ host test only; FDTabl | ❌ no ring-3 task perform | — pure-software table | — |
| cwd (sys_getcwd/sys_chdir, current->cwd) | ⚠️ test/unit/test_cwd_sta | ❌ no integration test li | 🟡 kernel/test/test_cwd_s | ❌ no-op; per-CPU current | ❌ no ring-3 task issues | — pure-software cwd stri | M3 |

## 假测清单（⚠️ 共 36 条，F-VERIFY 优先消除）

> 「绿但不证明任何事」的测试。每条：为何假 + 消除方案 + 责任批。

| 测试 | 为何假 | 消除方案 | 批 |
|------|--------|----------|----|
| test/unit/test_pmm.cpp (host) — all PMM ca | Mirror copy: re-implements PMM logic in the test instead of linking real pmm.cpp | Convert to add_cinux_integration_test linking kernel/mm/pmm. | M3 |
| test/unit/test_vmm.cpp (host) — VMM map/tr | Mirror copy of the 4-level walk on a host pool; real g_vmm never linked/proven o | Link kernel/mm/vmm.cpp into the integration test, or drop th | M4 |
| test/unit/test_address_space.cpp (host) | Mirror copy using MockPMM + reimplemented walker; proves nothing about real Addr | Link address_space.cpp (deps: vmm.cpp/pmm.cpp) into a host i | M5 |
| test/unit/test_fork_exec.cpp cow_pte:* (ho | Sentinel-phys bare-struct bit manipulation — cannot reach handle_cow_fault (proc | Add a test that builds a real AddressSpace, marks a writable | M2 |
| kernel/test/test_clone.cpp test_clone_vm_s | Sentinel addr_space pointer; CLONE_VM share is checked as pointer-equality of a  | Use a real AddressSpace for the parent task so clone's CoW/s | M2 |
| kernel/test/test_pmm_mapcount.cpp test_sim | Simulated lifecycle on the PMM API alone — does not invoke fork()/clone()/handle | Drive a real fork() producing a child AddressSpace and asser | M2 |
| test/unit/test_scheduler.cpp (host RoundRo | Mirror copy: re-implements RoundRobin enqueue/pick_next and Scheduler logic in t | Convert to add_cinux_integration_test linking real roundrobi | M2 |
| test/unit/test_fork_exec.cpp (host fork/Co | Claims to cover "fork/CoW (034)" but links only PidAllocator + elf_types. The Co | Drop the CoW claims from this test (leave it as a pure PidAl | M2 |
| kernel/test/test_fork_exec.cpp::test_dispa | Calls real sys_fork but with addr_space==nullptr so the entire CoW page-table co | Give tmp a real AddressSpace with at least one mapped user p | M3 |
| kernel/test/test_clone.cpp::test_clone_vm_ | CLONE_VM assertion (line 149) checks child->addr_space==parent->addr_space==0x12 | Allocate a real AddressSpace on the parent so CLONE_VM share | M3 |
| kernel/test/test_fork_exec.cpp::test_cow_p | Proves only that FLAG_COW is bit 9 and combinable/clearable on a synthetic value | After a real fork (with addr_space set), walk the child's PM | M3 |
| kernel/test/test_fork_exec.cpp::test_dispa | Guaranteed to take the early-error branch of execve (NoAddressSpace/NoCurrentTas | Mount ext2 + give tmp a real AddressSpace, point path at a r | M3 |
| test/unit/test_syscall.cpp (all 'syscall:  | Mirror copy: dispatch/register/write/exit logic duplicated in mock::, not linked | Convert to add_cinux_integration_test linking syscall.cpp (n | M2 |
| test/unit/test_gdt_idt.cpp (gdt/idt mirror | Mirror of private encoding logic: a change to real gdt.cpp segment_entry / idt.c | Expose GDT/IDT entry construction via a testable friend/inte | M4 |
| test/unit/test_pic.cpp (pic mask/unmask/eo | Mirror of PIC mask/unmask bit logic against simulated IMR bytes; real pic.cpp us | Delete in favor of the real kernel/test/test_pic_pit.cpp, or | M3 |
| test/unit/test_usermode.cpp (usermode cons | Mirror/constant-only: never links usermode.cpp/usermode.S, never performs a ring | Replace TSS mirror with static_assert against the real GDT:: | M5 |
| kernel/test/test_usermode.cpp test_tss_rsp | Sentinel/no-op assertions: tss_set_rsp0 writes RSP0 but the test only asserts it | Add an accessor (e.g. GDT::tss_rsp0() / ist[n]()) or read th | M4 |
| kernel/test/test_usermode.cpp test_msr::te | Not a readback: the comment explicitly states QEMU drops the SFMASK write so rdm | On real HW (or when a TCG fix exists) rdmsr SFMASK and asser | M2 |
| test/unit/test_ahci.cpp (AHCI host unit: C | Mirror copy: re-implements the kernel's FIS/LBA/PRDT encoding instead of linking | Link kernel/drivers/ahci/ahci.cpp via add_cinux_integration_ | M2 |
| test/unit/test_ahci_write.cpp (AHCI write  | Mirror copy: re-implements CFIS build and ext2 LBA math; never links ahci.cpp or | add_cinux_integration_test linking the real build_cfis + ext | M2 |
| test/unit/test_keyboard.cpp (PS/2 scan-cod | Mirror copy: re-implements the scan-code decoder and ring buffer; never links ke | Either drop the host mirror (the kernel-test already covers  | M2 |
| test/unit/test_mouse.cpp + test/unit/test_ | Mirror copy re-implementing mouse/HID decode; the kernel-test defers to it creat | Link mouse.cpp/usb_mouse.cpp decode logic in a host integrat | M2 |
| test/unit/test_xhci.cpp sections 5-8 (xHCI | Partially fake: the controller state machine (xhci_controller.cpp init/start/enu | add_cinux_integration_test linking xhci_controller.cpp with  | M2 |
| test/unit/test_ata.cpp (ATA PIO register e | Mirror copy re-implementing ATA LBA/head register encoding; no kernel ATA driver | Link the real ATA encoder or drop if ATA PIO is superseded b | M2 |
| kernel/test/test_dma_buffer.cpp (DmaBuffer | White-box: DmaBuffer constructed over a fake backing pointer, not a real DMA all | Use g_dma_pool.alloc() (as test_dma_pool.cpp does) to back t | M4 |
| kernel/test/test_mouse_event.cpp (Mouse dr | Coverage deferral loop: kernel-test claims host-test covers Mouse::init/IRQ; hos | Add a real-QEMU PS/2 mouse packet-inject test (port 0x60/0x6 | M2 |
| test/unit/test_ext2_ops.cpp (ext2 write/cr | MIRROR copy: ext2 write/create/unlink logic is duplicated in the test (own alloc | Link the real ext2_directory.cpp / ext2_inode.cpp via add_ci | M2 |
| test/unit/test_ext2_allocator.cpp (ext2 bl | MIRROR copy: bitmap scanning algorithm duplicated in the test instead of linking | Link real ext2_common.cpp behind a host mock block device, o | M2 |
| test/unit/test_syscall_ext2.cpp (sys_creat | MIRROR copy: split_pathname + canonical-address validation + syscall dispatch re | Link real sys_creat/sys_mkdir/sys_unlink dispatch (or the re | M2 |
| test/unit/test_ramdisk.cpp (ramdisk / usta | MIRROR copy disguised as an integration test: the integration_test registration  | Either demote to add_cinux_test (honest pure-math) or refact | M2 |
| test/unit/test_ext2_inode_ops.cpp (InodeOp | PARTIAL/semi-real: it links and tests the REAL Inode/InodeOps vtable dispatch (g | Rename to test_inode_ops_dispatch (honest) and/or add a vari | M2 |
| test/unit/test_cwd_stat.cpp (host cwd_stat | Mirror copy: re-implements path logic + struct stat locally instead of linking t | Convert to add_cinux_integration_test linking real kernel/fs | M3 |
| test_creds::test_fork_inherits_via_memcpy  | Simulates the production inheritance path with a hand memcpy instead of invoking | Add a real fork()/clone() test that creates a parent with no | M4 |
| test/unit/test_initial_stack.cpp (host ini | Borderline: it DOES exercise the real inline build_initial_stack logic (header c | Keep as the structural layout test, but add a ring-3 test (v | M5 |
| kernel/test/test_user_ptr.cpp (UserPtr mar | Type-level scaffold only. It proves the marker holds/converts a pointer (trivial | Add tests for validate_user_ptr() (canonical-address accept/ | M4 |
| (absent) stack canary readback / negative  | No test exists at all — SSP could be silently broken (guard never seeded, or __s | Add (a) a readback test that prints/asserts __stack_chk_guar | M2 |

## 机制回读索引（CR/MSR/EFER 位 → 回读测试 → 目标 CPU）

> 38 个使能位。audit 发现仅 `test_usermode::test_f9` 一处真回读（BSP 单核）。**AP 侧零回读**是 SMEP/SMAP 4 批假绿 + LSTAR==0 #DF 的同根。新功能使能**签收必填一行**（写进 DIRECTIVES）。

| 位 | 使能位置 | 回读测试 | BSP | AP |
|----|----------|----------|-----|-----|
| CR3 (current PML4 root) | address_space.cpp:151 write_cr3(pm | main_test.cpp:253-266 read | 🟡 read back (CR3 printed + PML4[272] deref, identity probe at 2/8/16 MB) | ❌ no readback (APs never woken in run-kernel-test-smp) |
| EFER.NXE (bit 11) — NX p | kernel/arch/x86_64/usermode.S:62-6 | none — no MM/VMM test read | ❌ no readback of EFER.NXE | ❌ no readback |
| EFER.SCE (bit 0) — SYSCA | kernel/arch/x86_64/usermode.S:62-6 | none in MM cluster | ❌ (covered only outside cluster, indirectly) | ❌ |
| Direct-map 1 GB huge pag | boot loader maps all RAM at DIRECT | main_test.cpp:253-266 read | 🟡 identity verified low-phys; PS/huge bit not explicitly read back from a PDE | ❌ no readback |
| CR4.SMEP/SMAP/OSFXSR/OSX | main_test.cpp:~243 enable_smep_sma | test_usermode.cpp:122-146  | ✅ (read in test_usermode, not an MM test, but it gates MM user-page access) | ❌ no AP-side CR4 readback |
| CR4.SMEP (bit 20) | kernel/arch/x86_64 boot.S enable_s | kernel/test/test_usermode. | ✅ readback | ❌ no readback |
| CR4.SMAP (bit 21) | boot.S enable_smep_smap() (CPUID-g | kernel/test/test_usermode. | ✅ readback | ❌ no readback |
| CR4.OSFXSR (bit 9) + OSX | boot.S (BSP) and ap_trampoline.S ( | kernel/test/test_usermode. | ✅ readback | ❌ no readback |
| EFER.SCE (bit 0) | kernel/arch/x86_64 usermode.S user | kernel/test/test_usermode. | ✅ readback | ❌ no readback (AP-only #DF class bug — smp-shell-lstar memory) |
| EFER.NXE (bit 11) | long-mode setup | kernel/test/test_usermode. | ✅ readback | ❌ no readback |
| STAR (0xC0000081) | usermode.S usermode_init_asm; AP:  | kernel/test/test_usermode. | ✅ readback | ❌ no readback |
| LSTAR (0xC0000082) | syscall_init() (BSP); AP: ap_main. | kernel/test/test_syscall.c | ✅ readback | ❌ no readback — LSTAR-on-AP (the exact bug that crashed shell) is never verified live |
| SFMASK (0xC0000084) IF-c | usermode_init_asm; AP: ap_main.cpp | kernel/test/test_syscall.c | 🟡 (no-GP only, value not read back) | ❌ no readback |
| MSR_FS_BASE (0xC0000100) | kernel/arch/x86_64/context_switch. | kernel/test/test_tls.cpp:4 | ✅ readback (direct MSR, not via switch) | ❌ no readback |
| CR4.OSFXSR (bit 9) | boot.S:51-53 (BSP, or $((1<<9)/(1< | kernel/test/test_usermode. | ✅ read back (unconditional assert) | ❌ no readback |
| CR4.OSXMMEXCPT (bit 10) | boot.S:51-53 (BSP); ap_trampoline. | kernel/test/test_usermode. | ✅ read back (unconditional assert) | ❌ no readback |
| CR4.SMEP (bit 20) | paging.cpp:47-58 enable_smep_smap( | kernel/test/test_usermode. | 🟡 read back but gated — skipped if CPUID leaf7 absent (WSL2 -cpu max hides it) | ❌ no readback |
| CR4.SMAP (bit 21) | paging.cpp:47-58 enable_smep_smap( | kernel/test/test_usermode. | 🟡 read back but gated — skipped if CPUID leaf7 absent | ❌ no readback |
| CR4.PAE (bit 5, 0x20) | ap_trampoline.S:91 (AP, orl $0x620 | none (no test reads CR4.PA | ❌ no readback | ❌ no readback |
| EFER.NXE (bit 11) | usermode.S:62-65 (BSP, orq $(1/(1< | kernel/test/test_usermode. | ✅ read back | ❌ no readback — and AP enable site for NXE is missing/implicit (trampoline sets only EFER.LME at ap_trampoline.S:101-105, not NXE bit11) |
| EFER.SCE (bit 0) | usermode.S:62-65 (BSP, SCE/NXE); A | kernel/test/test_usermode. | ✅ read back | ❌ no readback on AP |
| EFER.LME (bit 8) | ap_trampoline.S:101-105 (AP); BSP  | none (no test reads EFER.L | ❌ no readback (implicit — long mode is running) | ❌ no readback |
| MSR_STAR (0xC0000081) [6 | syscall.cpp:161 write_msr(MSR_STAR | kernel/test/test_usermode. | ✅ read back | ❌ no readback |
| MSR_LSTAR (0xC0000082) = | syscall.cpp:164 write_msr(MSR_LSTA | none — LSTAR NEVER read ba | ❌ no readback | ❌ no readback |
| MSR_SFMASK (0xC0000084)  | syscall.cpp:167 write_msr(MSR_SFMA | kernel/test/test_usermode. | ⚠️ fake readback (QEMU drops value) | ❌ no readback |
| MSR_GS_BASE (0xC0000100) | ap_main.cpp:126 write_msr(kMsrGsBa | none (no test reads GS_BAS | ❌ no readback | ❌ no readback |
| USBCMD.RS/INTE (xHCI Run | kernel/drivers/usb/xhci_controller | kernel/test/test_xhci.cpp: | 🟡 status-readback only (USBSTS, not USBCMD bit). No USBCMD re-read to prove INTE set | ❌ no AP exercises xHCI |
| USBSTS.EINT (xHCI event  | controller-asserted after INTE+doo | kernel/test/test_xhci.cpp: | ✅ EINT observed on BSP (controller-side) | ❌ no AP run; MSI-X->CPU delivery not proven (test comment :72-74) |
| MSI-X Message Control En | kernel/drivers/pci/msix_controller | NONE. No test re-reads MC  | ❌ no readback | ❌ no readback; AP-targeted vector (dest_apic_id) program_vector at msix_controller.cpp:89-100 never verified |
| MSI-X Table entry vector | kernel/drivers/pci/msix_controller | NONE. The :98 read-back is | ❌ no test reads the Table entry back | ❌ no AP programming test |
| e1000 RCTL/CTRL (RX enab | kernel/drivers/net/e1000.cpp/e1000 | kernel/test/test_e1000.cpp | 🟡 data-path inferred (RX works) but RCTL/CTRL enable bits never read back | ❌ no AP |
| AHCI GHC.AE (AHCI Enable | kernel/drivers/ahci/ahci.cpp init( | kernel/test/test_ahci.cpp: | ❌ no enable-bit readback | ❌ no AP |
| PCI Command register (Bu | kernel/drivers/pci/pci.cpp sets CO | NONE. No test re-reads a d | ❌ no readback | ❌ no readback |
| i8042 PS/2 config/mode ( | kernel/drivers/keyboard/keyboard.c | kernel/test/test_keyboard. | 🟡 data-path only (decode works), no config-bit readback | ❌ no AP |
| (none — software-only cl | A5-fs-lib (ext2/vfs/ramdisk/pipe/k | none — no CR/MSR/EFER bit  | — | — |
| GCC frame-pointer (%rbp  | arch/x86_64/backtrace.cpp:110 read | kernel/test/test_backtrace | 🟡 BSP-only: %rbp readback + frame walk asserted on BSP in qemu-kernel. | ❌ AP-side backtrace/panic symbolization never tested (run-kernel-test-smp is a no-op; the F4 AP-LSTAR bug class shows AP fault/panic paths diverge). |
| __stack_chk_guard (SSP c | seeded from rdtsc in kernel/arch/x | NONE — no test reads __sta | ❌ no readback | ❌ no readback (and global guard is BSP-seeded once at boot; AP-side would inherit the same global, but never verified) |
| (none other in cluster) | ASLR/creds/user_ptr/initial_stack/ | n/a | — | — |

## M1 收官 + 后续

**M1 完成（2026-06-27）**：47 子系统 × 6 维度全量 grep 坐实；36 假测、38 机制位登记。三大结构性盲区量化坐实：
1. **host-integration 真码链接 37/47 ❌** —— 大量 host 测试是镜像副本（test_pmm/vmm/address_space/scheduler 重新实现逻辑），改真码不跟着测。→ **M2** 消镜像、链接真码。
2. **QEMU-SMP 47/47 ❌（空转）** —— run-kernel-test-smp 不 boot_aps，SMP 门名存实亡；所有 SMP-only bug（迁移竞态、CoW 跨核 UAF、LSTAR-AP）CI 永远抓不到。→ **M3** 真唤醒 AP。
3. **机制回读 ~27/47 ❌ + AP 侧零回读** —— 使能位没读回断言，SMEP/SMAP 假绿类根治不了。→ **M4** 回读矩阵 + AP per-cpu 结果槽。

本表为 F-VERIFY 追踪表（同 debt.md 之于 F-QA/F-CLN）：M2/M3/M4/M5/M6 各批补的测试回头把 ⚠️/❌ 升 ✅，新功能签收必须填一行。