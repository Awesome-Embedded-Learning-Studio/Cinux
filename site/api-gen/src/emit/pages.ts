// 页面骨架生成:ApiBundle → document/api/ 下的 markdown。
//
// 渲染策略:骨架用 markdown 而非纯 Vue 页面——签名走 ```cpp 代码块
// (Shiki 高亮白送),参数表走 md 表格,整页自动进 VitePress 本地搜索
// 索引。交互组件(M1 之后)按需在骨架上叠。
//
// 页面粒度:组页(42)+首页(1)。符号独立页推迟——1025 页会把分卷构建
// 拖到十几分钟,组页内锚点(#签名)已能深链定位。

import { mkdirSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import type { ApiBundle, ApiSymbol } from '../model/symbol'

const KIND_ORDER = ['function', 'variable', 'enum', 'typedef']
const KIND_TITLES: Record<string, string> = {
  function: '函数',
  variable: '变量',
  enum: '枚举',
  typedef: '类型别名',
  friend: '友元',
}

/** 区块前缀(base_ / boot_ / kernel_ → 中文分区) */
const SECTIONS: { match: (g: string) => boolean, title: string, blurb: string }[] = [
  {
    match: g => g.startsWith('base_'),
    title: 'Cinux-Base 基础库',
    blurb: '零 OS 耦合的 header-only 基础类型:容器、格式化、结果类型与位操作,内核与未来项目共用。',
  },
  {
    match: g => g.startsWith('boot_'),
    title: '引导过程',
    blurb: '从 BIOS/MBR 到长模式的引导链:内存探测、GDT、VESA 与内核加载。',
  },
  {
    match: g => g.startsWith('kernel_'),
    title: '内核子系统',
    blurb: '运行中的内核:内存管理、进程调度、文件系统、驱动、网络与系统调用。',
  },
  {
    match: () => true,
    title: '其他',
    blurb: '',
  },
]

function esc(text: string): string {
  // md 表格与正文里的管道符转义
  return text.replace(/\|/g, '\\|')
}

function anchorOf(s: ApiSymbol): string {
  const base = s.qualifiedName.toLowerCase().replace(/[^a-z0-9]+/g, '-')
  // 重载函数 qualifiedName 相同,拼 doxygen id 的 hash 尾部消歧
  const tail = s.id.slice(-6)
  return `${base}-${tail}`
}

function githubLink(s: ApiSymbol, repo: { owner: string, repo: string, branch: string }): string {
  if (!s.location.file) return ''
  return `https://github.com/${repo.owner}/${repo.repo}/blob/${repo.branch}/${s.location.file}#L${s.location.line}`
}

function formatParams(s: ApiSymbol): string {
  if (s.params.length === 0) return ''
  const hasDesc = s.params.some(p => p.description)
  if (!hasDesc) return ''
  const dirMark = (d?: string) =>
    d ? `\`${d}\`` : ''
  const rows = s.params
    .map(p => `| \`${esc(p.name)}\` | ${dirMark(p.direction)} | ${esc(p.description)} |`)
    .join('\n')
  return `| 参数 | 方向 | 说明 |\n|---|---|---|\n${rows}\n`
}

function symbolSection(s: ApiSymbol, repo: { owner: string, repo: string, branch: string }): string {
  const parts: string[] = []
  parts.push(`### ${esc(s.qualifiedName)} {#${anchorOf(s)}}`)
  parts.push('')
  parts.push('```cpp')
  parts.push(esc(s.signature) + (s.kind === 'function' && s.signature.includes('(') ? '' : ''))
  parts.push('```')
  parts.push('')
  if (s.brief) {
    parts.push(esc(s.brief))
    parts.push('')
  }
  if (s.details) {
    parts.push(esc(s.details))
    parts.push('')
  }
  const table = formatParams(s)
  if (table) {
    parts.push(table)
    parts.push('')
  }
  if (s.returns && !/^(none|无)$/i.test(s.returns)) {
    parts.push(`**返回** ${esc(s.returns)}`)
    parts.push('')
  }
  if (s.note) {
    parts.push(`> **注** ${esc(s.note)}`)
    parts.push('>')
    parts.push('')
  }
  if (s.warning && !/^(none|无)$/i.test(s.warning)) {
    parts.push(`> **警告** ${esc(s.warning)}`)
    parts.push('')
  }
  const meta: string[] = []
  if (s.since) meta.push(`自 \`${s.since}\``)
  const link = githubLink(s, repo)
  if (link) meta.push(`[源码 ${s.location.file}:${s.location.line}](${link})`)
  if (meta.length > 0) {
    parts.push(meta.join(' · '))
    parts.push('')
  }
  return parts.join('\n')
}

function variablesTable(vars: ApiSymbol[], repo: { owner: string, repo: string, branch: string }): string {
  const rows = vars
    .map(v => {
      const link = githubLink(v, repo)
      const src = link ? `[\`${v.location.file}:${v.location.line}\`](${link})` : ''
      const since = v.since ? `\`${v.since}\`` : ''
      return `| \`${esc(v.name)}\` | \`${esc(v.returnType)}\` | ${esc(v.brief)} | ${since} | ${src} |`
    })
    .join('\n')
  return `| 名称 | 类型 | 说明 | 自 | 源码 |\n|---|---|---|---|---|\n${rows}\n`
}

function groupPage(
  group: { name: string, title: string, symbolIds: string[] },
  symbols: ApiSymbol[],
  coverage: ApiBundle['coverage'],
  repo: { owner: string, repo: string, branch: string },
): string {
  const byId = new Map(symbols.map(s => [s.id, s]))
  const members = group.symbolIds.map(id => byId.get(id)).filter((s): s is ApiSymbol => !!s)
  const cov = coverage.find(c => c.group === group.name)

  const kinds = new Set(members.map(m => m.kind))
  const ordered = KIND_ORDER.filter(k => kinds.has(k))

  const lines: string[] = []
  lines.push('---')
  lines.push(`title: ${group.title}`)
  lines.push('---')
  lines.push('')
  lines.push(`# ${group.title}`)
  lines.push('')
  lines.push(`\`@ingroup ${group.name}\``)
  const stats: string[] = [`${members.length} 个符号`]
  if (cov) {
    stats.push(`文档覆盖 ${cov.withBrief}/${cov.total}`)
    if (cov.paramDocs) stats.push(`参数说明 ${cov.paramDocs.covered}/${cov.paramDocs.total}`)
  }
  lines.push('')
  lines.push(stats.join(' · '))
  lines.push('')

  for (const kind of ordered) {
    const pool = members.filter(m => m.kind === kind)
    lines.push(`## ${KIND_TITLES[kind] ?? kind}`)
    lines.push('')
    if (kind === 'variable' || kind === 'typedef') {
      // 紧凑表:变量/别名不占独立小节
      lines.push(variablesTable(pool, repo))
    } else {
      for (const s of pool) {
        lines.push(symbolSection(s, repo))
      }
    }
  }
  // 未归类的 kind(friend 等)
  const rest = members.filter(m => !KIND_ORDER.includes(m.kind))
  if (rest.length > 0) {
    lines.push(`## 其他`)
    lines.push('')
    for (const s of rest) {
      lines.push(symbolSection(s, repo))
    }
  }
  return lines.join('\n')
}

function homePage(bundle: ApiBundle): string {
  const lines: string[] = []
  lines.push('---')
  lines.push('title: API 参考')
  lines.push('---')
  lines.push('')
  lines.push('# API 参考')
  lines.push('')
  lines.push('按 `@ingroup` 分组的接口参考,数据由源码注释生成。每个分组页内含函数签名、参数表与源码位置。')
  lines.push('')

  // 每组只归第一个匹配的分区;兜底 section(()=> true)只收前面没接住的组
  const sectionOf = new Map(bundle.groups.map(g => [g.name, SECTIONS.find(sec => sec.match(g.name))!]))
  for (const section of SECTIONS) {
    const groups = bundle.groups.filter(g => sectionOf.get(g.name) === section)
    if (groups.length === 0) continue
    lines.push(`## ${section.title}`)
    lines.push('')
    if (section.blurb) {
      lines.push(section.blurb)
      lines.push('')
    }
    const byId = new Map(bundle.symbols.map(s => [s.id, s]))
    const rows = groups
      .map(g => {
        const n = g.symbolIds.length
        const sample = g.symbolIds
          .map(id => byId.get(id))
          .filter((s): s is ApiSymbol => !!s && s.kind === 'function' && s.documented)
          .slice(0, 3)
          .map(s => '`' + s.name + '`')
          .join(' ')
        return `| [${esc(g.title)}](/api/${g.name}/) | ${n} | ${sample} |`
      })
      .join('\n')
    lines.push(`| 分组 | 符号 | 代表接口 |`)
    lines.push(`|---|---|---|`)
    lines.push(rows)
    lines.push('')
  }
  return lines.join('\n')
}

export interface PageEmitOptions {
  outDir: string
  repo: { owner: string, repo: string, branch: string }
}

export function emitPages(bundle: ApiBundle, opts: PageEmitOptions): string[] {
  // 清掉旧页(组名改名后会留孤儿)
  rmSync(opts.outDir, { recursive: true, force: true })
  mkdirSync(opts.outDir, { recursive: true })

  const written: string[] = []
  for (const group of bundle.groups) {
    const dir = join(opts.outDir, group.name)
    mkdirSync(dir, { recursive: true })
    const file = join(dir, 'index.md')
    writeFileSync(file, groupPage(group, bundle.symbols, bundle.coverage, opts.repo))
    written.push(file)
  }
  const home = join(opts.outDir, 'index.md')
  writeFileSync(home, homePage(bundle))
  written.push(home)
  console.log(`pages: ${written.length} 个页面 → ${opts.outDir} (现有 ${readdirSync(opts.outDir).length} 项)`)
  return written
}
