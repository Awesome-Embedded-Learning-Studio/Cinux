# Kernel test kernels: one entry "module/name:stage" = one test file at
# test/kernel/<module>/test_<name>.cpp = one kernel = one image = one QEMU
# boot = one CTest case. The stage names the init-ladder level the test main
# walks before running cases.
set(CINUX_KTEST_STAGES exceptions irq keyboard mm proc block)
set(CINUX_KTESTS kernel/selfcheck:exceptions drivers/console:exceptions time/tick:irq
    drivers/keyboard:keyboard mm/vmm:mm mm/heap:mm mm/aspace:mm mm/cow:mm
    mm/vma:mm mm/demand:mm loader/loader:mm proc/uaccess:proc proc/sched:proc
    drivers/block:block drivers/block_error:block fs/ext2:block
    proc/pipe:proc drivers/tty:keyboard proc/smp:proc proc/smp_pipe:proc
    drivers/xhci:block net/net:block)
set(CINUX_KTESTS_INTEGRATION "")
set(CINUX_KTESTS_DUAL_CPU block smp smp_pipe)
