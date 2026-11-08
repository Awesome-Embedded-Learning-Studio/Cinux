<#
.SYNOPSIS
    在 Windows 上启动 Cinux。

.DESCRIPTION
    自动找到 qemu-system-x86_64（PATH 或默认安装目录），优先使用 WHPX
    硬件加速。WHPX 不可用时退回 TCG 软件模拟，并在启动前打印提示。
    默认从 build\boot 下取内核镜像 cinux.img 和 ext2 根盘 ext2.img。

    WHPX 需要 Windows 11、BIOS/UEFI 里开启虚拟化，并在“启用或关闭
    Windows 功能”里勾选“Windows 虚拟机监控程序平台”。QEMU 版本太旧也
    会缺 WHPX，建议 8.0 以上。

.PARAMETER Kernel
    内核启动镜像，默认 <仓库>\build\boot\cinux.img。

.PARAMETER Disk
    ext2 根文件系统盘，默认 <仓库>\build\boot\ext2.img。

.PARAMETER Memory
    虚拟机内存，单位 MiB，默认 1024。

.PARAMETER Cpus
    SMP 核数，默认 2。

.PARAMETER Accel
    auto（默认）先试 WHPX 再退 TCG。whpx 强制用 WHPX，不可用就报错。
    tcg 强制用软件模拟。

.EXAMPLE
    .\run-windows.ps1

.EXAMPLE
    .\run-windows.ps1 -Kernel .\build\boot\cinux.img -Disk .\build\boot\ext2.img -Memory 2048

.EXAMPLE
    .\run-windows.ps1 -Accel tcg
#>
[CmdletBinding()]
param(
    [string]$Kernel = '',
    [string]$Disk = '',
    [int]$Memory = 1024,
    [int]$Cpus = 2,
    [ValidateSet('auto', 'whpx', 'tcg')]
    [string]$Accel = 'auto'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Kernel) {
    $Kernel = Join-Path $RepoRoot 'build\boot\cinux.img'
}
if (-not $Disk) {
    $Disk = Join-Path $RepoRoot 'build\boot\ext2.img'
}

# qemu 可能不在 PATH 里，默认安装器会放到 Program Files\qemu。
function Find-Qemu {
    $command = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $roots = @()
    if ($env:ProgramFiles) {
        $roots += $env:ProgramFiles
    }
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if ($programFilesX86) {
        $roots += $programFilesX86
    }
    foreach ($root in $roots) {
        $candidate = Join-Path $root 'qemu\qemu-system-x86_64.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw ('找不到 qemu-system-x86_64。请先安装 QEMU（https://www.qemu.org/download/#windows），' +
           '安装时勾选加入 PATH，或用 PATH 指向已有的 qemu.exe。')
}

# 用 -machine none -S 起一个不接设备的空机器：加速器初始化失败会立刻
# 退出并写 stderr，能起来就说明这个加速器在这台机器上可用。1.5 秒足够
# 覆盖正常的 WHPX/QEMU 初始化，超时按可用处理。
function Test-Accelerator {
    param(
        [Parameter(Mandatory = $true)][string]$Qemu,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $outFile = [System.IO.Path]::GetTempFileName()
    $errFile = [System.IO.Path]::GetTempFileName()
    try {
        $probeArgs = @(
            '-accel', $Name,
            '-machine', 'none',
            '-display', 'none',
            '-monitor', 'none',
            '-serial', 'none',
            '-S'
        )
        $process = Start-Process -FilePath $Qemu -ArgumentList $probeArgs -PassThru `
            -NoNewWindow -RedirectStandardOutput $outFile -RedirectStandardError $errFile
        for ($attempt = 0; $attempt -lt 15; $attempt++) {
            Start-Sleep -Milliseconds 100
            if ($process.HasExited) {
                break
            }
        }
        if (-not $process.HasExited) {
            try {
                $process.Kill()
                $process.WaitForExit()
                return @{ Supported = $true; Detail = '' }
            }
            catch {
                # 进程在探测窗口末尾自己退出了，按不可用处理，退回 TCG 更稳。
            }
        }

        $detail = ''
        if (Test-Path -LiteralPath $errFile) {
            $detail = [string](Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue)
        }
        return @{ Supported = $false; Detail = $detail.Trim() }
    }
    finally {
        Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $errFile -Force -ErrorAction SilentlyContinue
    }
}

foreach ($image in @($Kernel, $Disk)) {
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
        throw ("找不到 $image。先在 Linux/WSL2 里构建，生成 build\boot\cinux.img 和 " +
               'build\boot\ext2.img，或者用 -Kernel、-Disk 指到别处。')
    }
}
$Kernel = (Resolve-Path -LiteralPath $Kernel).Path
$Disk = (Resolve-Path -LiteralPath $Disk).Path

$qemu = Find-Qemu
$accelerator = $Accel
if ($Accel -eq 'auto') {
    $whpx = Test-Accelerator -Qemu $qemu -Name 'whpx'
    if ($whpx.Supported) {
        $accelerator = 'whpx'
    }
    else {
        $accelerator = 'tcg'
        Write-Warning ('没有可用的 WHPX，改用 TCG 软件模拟，速度会慢很多。' +
                       '要硬件加速请在 Windows 功能里启用“Windows 虚拟机监控程序平台”。')
        if ($whpx.Detail) {
            Write-Verbose $whpx.Detail
        }
    }
}
elseif ($Accel -eq 'whpx') {
    $whpx = Test-Accelerator -Qemu $qemu -Name 'whpx'
    if (-not $whpx.Supported) {
        throw ('WHPX 不可用：' + $whpx.Detail + ' 请启用“Windows 虚拟机监控程序平台”，' +
               '或改用 -Accel tcg。')
    }
}

# WHPX 和 TCG 都不能借用宿主 CPU 型号，统一用 max。
$cpu = 'max'

$qemuArgs = @(
    '-accel', $accelerator,
    '-cpu', $cpu,
    '-smp', "$Cpus",
    '-m', "${Memory}M",
    '-display', 'none',
    '-no-reboot',
    '-monitor', 'none',
    '-serial', 'stdio',
    '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
    '-device', 'ahci,id=sata0',
    '-drive', "format=raw,file=$Disk,if=none,id=spare",
    '-device', 'ide-hd,drive=spare,bus=sata0.0',
    '-device', 'qemu-xhci,id=xhci0',
    '-device', 'usb-kbd,bus=xhci0.0',
    '-device', 'usb-tablet,bus=xhci0.0',
    '-netdev', 'user,id=net0',
    '-device', 'e1000,netdev=net0',
    '-drive', "format=raw,file=$Kernel"
)

Write-Host "QEMU:     $qemu"
Write-Host "加速器:   $accelerator (-cpu $cpu)"
Write-Host "内核镜像: $Kernel"
Write-Host "ext2 盘:  $Disk"
Write-Host "内存:     ${Memory} MiB / SMP $Cpus"
Write-Host '按 Ctrl+C 退出。'
& $qemu @qemuArgs
exit $LASTEXITCODE
