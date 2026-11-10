// 组存根生成:doxygen 的 @ingroup 只认被 @defgroup 声明过的组。
//
// 仓库源码里有 42 个组名被 @ingroup 引用、0 个 @defgroup(注释体系把
// 分组当索引用,没人负责声明),直接跑 doxygen 会把所有 @ingroup 静默
// 忽略,XML 里就没有分组数据。
//
// 这一步在 doxygen 之前跑:扫描源码收集全部 @ingroup 组名,生成一份
// 存根 .dox 文件,每个组一行 @defgroup。组的标题与说明由后续批次在
// 这里维护(见 GROUP_TITLES),没维护的组先拿组名当标题。
//
// 用法:tsx site/api-gen/src/generate-groups.ts

import { mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs'
import { join, resolve } from 'node:path'

const PROJECT_ROOT = resolve(import.meta.dirname, '../../..')
const SOURCE_DIRS = ['kernel', 'base', 'boot', 'user']
const OUT_FILE = join(PROJECT_ROOT, 'site/api-gen/generated/groups.dox')

/** 组名 → 展示标题;未列出的组用组名本身 */
const GROUP_TITLES: Record<string, string> = {
  // base_*
  base_addr: '地址类型 (Addressing)',
  base_async: '异步原语 (Async)',
  base_bit_ops: '位操作 (Bit Operations)',
  base_bytes: '字节序 (Byte Order)',
  base_container: '容器 (Containers)',
  base_format: '格式化 (Formatting)',
  base_literals: '字面量 (Literals)',
  base_math: '数学 (Math)',
  base_memory: '内存工具 (Memory Utilities)',
  base_page: '页描述 (Page)',
  base_parse: '解析 (Parsing)',
  base_path: '路径 (Path)',
  base_ptr: '智能指针 (Smart Pointers)',
  base_random: '随机数 (Random)',
  base_region: '区域 (Region)',
  base_result: '结果类型 (Result)',
  base_safety: '安全检查 (Safety)',
  base_time: '时间 (Time)',
  base_utility: '通用工具 (Utility)',
  // boot_*
  boot_bios: 'BIOS 服务 (BIOS Services)',
  boot_e820: '内存探测 (E820)',
  boot_early: '早期引导 (Early Boot)',
  boot_gdt: 'GDT 与保护模式 (GDT)',
  boot_layout: '内存布局 (Memory Layout)',
  boot_lm: '长模式 (Long Mode)',
  boot_loader: '加载器 (Loader)',
  boot_mbr: '主引导记录 (MBR)',
  boot_vesa: 'VESA 视频模式 (VESA)',
  // kernel_*
  kernel_arch: '体系结构 (Architecture)',
  kernel_boot: '内核启动 (Kernel Boot)',
  kernel_debug: '调试设施 (Debug)',
  kernel_driver: '设备驱动 (Drivers)',
  kernel_fs: '文件系统 (Filesystems)',
  kernel_init: '内核初始化 (Init)',
  kernel_interrupt: '中断 (Interrupts)',
  kernel_lib: '内核库 (Kernel Library)',
  kernel_loader: '内核加载 (Kernel Loader)',
  kernel_mm: '内存管理 (Memory Management)',
  kernel_net: '网络 (Network)',
  kernel_proc: '进程与调度 (Process & Scheduling)',
  kernel_syscall: '系统调用 (System Calls)',
  kernel_time: '时间 (Time)',
}

function walk(dir: string, out: string[] = []): string[] {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    if (e.name.startsWith('.')) continue
    const full = join(dir, e.name)
    if (e.isDirectory()) walk(full, out)
    else if (/\.(hpp|h|cpp)$/.test(e.name)) out.push(full)
  }
  return out
}

const groups = new Set<string>()
let files = 0
for (const dir of SOURCE_DIRS) {
  const abs = join(PROJECT_ROOT, dir)
  const stat = statSync(abs, { throwIfNoEntry: false })
  if (!stat?.isDirectory()) continue
  for (const file of walk(abs)) {
    files++
    const text = readFileSync(file, 'utf-8')
    for (const m of text.matchAll(/@ingroup\s+([A-Za-z_][A-Za-z0-9_]*)/g)) {
      groups.add(m[1])
    }
  }
}

const known = new Set(Object.keys(GROUP_TITLES))
for (const g of known) {
  if (!groups.has(g)) {
    console.warn(`warn: GROUP_TITLES 里的 ${g} 在源码中已无 @ingroup 引用`)
  }
}

const lines = [
  '// 自动生成,不要手改:pnpm docs:api 会先跑 generate-groups.ts 重写此文件。',
  '// 组的标题在 site/api-gen/src/generate-groups.ts 的 GROUP_TITLES 里维护。',
  '',
]
for (const g of Array.from(groups).sort()) {
  const title = GROUP_TITLES[g] ?? g
  lines.push(`/** @defgroup ${g} ${title} */`)
  lines.push('')
}
mkdirSync(join(OUT_FILE, '..'), { recursive: true })
writeFileSync(OUT_FILE, lines.join('\n'))
console.log(`groups: ${groups.size} 个组(来自 ${files} 个源文件的 @ingroup)→ ${OUT_FILE}`)
