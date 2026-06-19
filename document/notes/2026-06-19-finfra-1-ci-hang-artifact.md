# F-INFRA I-1 — CI 防挂死 + 串口日志上传

> 日期 2026-06-19 · F-INFRA Tier0 批 I-1 · 分支 `feat/finfra`

## 背景
F-INFRA 完整性审查（G1/G8）抓到的"没有屋顶的房子"：所有调试增强（UBSAN/真实符号/mini-KASAN）的输出在 CI 里既看不见、还可能挂死 CI。`.github/workflows/ci.yml` 的 kernel-tests job 裸跑 `make run-kernel-test`，无 timeout 包裹；`scripts/qemu_test_wrapper.sh` 无 timeout；失败时串口输出（kprintf/panic/backtrace/memstats）只进 CI 步日志、不可下载。

## 目标
1. panic 死循环/死锁时 CI 快速失败（3 分钟而非吃满 15 分钟 job 预算）。
2. 失败时把完整 QEMU 串口日志作为 artifact 上传，便于事后取证。

## 设计/决策
- `timeout 180` 包裹 `make run-kernel-test`（正常运行 <60s，CI 余量足；挂死时 timeout 杀进程暴露非零退出码 124）。
- `2>&1 | tee serial.log`：串口（QEMU `-serial stdio`）落盘到 `build/serial.log`，同时仍实时显示。`set -o pipefail` 确保测试退出码穿透 tee（`qemu_test_wrapper.sh` 的 0=pass/1=fail 映射不丢）。
- `actions/upload-artifact@v4`，`if: failure()`：仅失败时上传（绿跑不上传，避免 artifact 噪声）。`if-no-files-found: ignore` 防边界。
- 保留 job 级 `timeout-minutes: 15` 作为兜底。

## 陷阱
- **串口日志被 grep 当二进制**：QEMU `-serial stdio` 流含控制字节（早期 boot/framebuffer 转义），`grep -c "ALL TESTS PASSED"` 输出被抑制（空），但 `tail` 可见、文件含 PASS 行。artifact 用途不受影响（事后 `tail`/`grep -a` 即可）。
- CI 步级 `timeout` 与本地 DIRECTIVES L5 的 `timeout 40` 是两回事：本地 40s 是开发约定防终端挂死；CI 用 180s 给慢 runner 余量。两者意图一致（挂死快失败）。

## 验证
- `python3 yaml.safe_load` → YAML OK。
- 本地 `set -o pipefail; timeout 40 make -C build run-kernel-test 2>&1 | tee serial.log` → PIPE_EXIT=0（840/0 绿），serial.log 257KB，含 `[TEST] ALL TESTS PASSED`。
- （CI upload-artifact 语法标准，本地无法跑 Actions，首次 push 时 CI 验。）

## 文件
- 改：`.github/workflows/ci.yml`（kernel-tests 的 "Run kernel tests" 步 + 新增 "Upload serial log on failure" 步）。
