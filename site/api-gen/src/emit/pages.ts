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
  parts.push(`#### ${esc(s.qualifiedName)} {#${anchorOf(s)}}`)
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
  if (s.referencedBy.length > 0) {
    const MAX_SHOW = 8
    const shown = s.referencedBy.slice(0, MAX_SHOW).map(r => `\`${esc(r.name)}\``)
    const rest = s.referencedBy.length - MAX_SHOW
    const suffix = rest > 0 ? ` 等共 ${s.referencedBy.length} 处` : ''
    parts.push(`**被调用** ${shown.join(' ')}${suffix}`)
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
      const brief = v.brief || '—'
      return `| \`${esc(v.name)}\` | \`${esc(v.returnType)}\` | ${esc(brief)} | ${since || '—'} | ${src} |`
    })
    .join('\n')
  return `| 名称 | 类型 | 说明 | 自 | 源码 |\n|---|---|---|---|---|\n${rows}\n`
}

function groupPage(
  group: { name: string, title: string, symbolIds: string[] },
  symbols: ApiSymbol[],
  coverage: ApiBundle['coverage'],
  repo: { owner: string, repo: string, branch: string },
  neighbors?: { prev?: { title: string, name: string }, next?: { title: string, name: string } },
): string {
  const byId = new Map(symbols.map(s => [s.id, s]))
  const members = group.symbolIds.map(id => byId.get(id)).filter((s): s is ApiSymbol => !!s)
  const cov = coverage.find(c => c.group === group.name)

  const kinds = new Set(members.map(m => m.kind))
  const ordered = KIND_ORDER.filter(k => kinds.has(k))

  // 符号索引:紧凑多列锚点列表,长组页(200+ 函数)的页内导航主力;
  // 函数小节用 h4,不进全局 outline,避免 244 项的大纲失控
  const indexLines: string[] = []
  for (const kind of ordered) {
    const pool = members.filter(m => m.kind === kind)
    if (pool.length === 0) continue
    indexLines.push(`**${KIND_TITLES[kind] ?? kind}**`)
    indexLines.push('')
    for (const m of pool) {
      indexLines.push(`- [${esc(m.name)}](#${anchorOf(m)})`)
    }
    indexLines.push('')
  }
  // details 默认收起:长组页(200+ 函数)的索引不占首屏,展开后多列排布
  const indexBlock = indexLines.length > 0
    ? `::: details 符号索引(${members.length})\n\n${indexLines.join('\n')}\n:::\n`
    : ''

  const lines: string[] = []
  lines.push('---')
  lines.push(`title: ${group.title}`)
  lines.push('---')
  lines.push('')
  lines.push(`# ${group.title}`)
  lines.push('')
  lines.push(`\`@ingroup ${group.name}\``)
  // 统计条:数字与徽章,覆盖率按档位给语义色(>=95 tip / >=70 info / 其余 warning)
  lines.push('')
  lines.push(`**${members.length}** 个符号`, '')
  if (cov) {
    const pct = (n: number, d: number) => (d === 0 ? 100 : Math.round((n / d) * 100))
    const briefPct = pct(cov.withBrief, cov.total)
    const briefType = briefPct >= 95 ? 'tip' : briefPct >= 70 ? 'info' : 'warning'
    lines.push(`<Badge type="${briefType}" text="文档覆盖 ${cov.withBrief}/${cov.total}（${briefPct}%）" />`)
    if (cov.paramDocs) {
      const paramPct = pct(cov.paramDocs.covered, cov.paramDocs.total)
      const paramType = paramPct >= 95 ? 'tip' : paramPct >= 70 ? 'info' : 'warning'
      lines.push('')
      lines.push(`<Badge type="${paramType}" text="参数说明 ${cov.paramDocs.covered}/${cov.paramDocs.total}（${paramPct}%）" />`)
    }
  }
  lines.push('')

  if (indexBlock) lines.push(indexBlock)
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
  // 分组间导航:上一组/下一组(组序与首页一致,按组名字母序)
  if (neighbors?.prev || neighbors?.next) {
    lines.push('---')
    lines.push('')
    const prev = neighbors.prev
      ? `← [${neighbors.prev.title}](/api/${neighbors.prev.name}/)`
      : '　'
    const next = neighbors.next
      ? `[${neighbors.next.title}](/api/${neighbors.next.name}/) →`
      : '　'
    lines.push(`| 上一组 | 下一组 |`)
    lines.push(`|---|---|`)
    lines.push(`| ${prev} | ${next} |`)
    lines.push('')
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
  lines.push('按 `@ingroup` 分组的接口参考,数据由源码注释生成。每个分组页内含函数签名、参数表与源码位置。接口是哪个版本引入的,见[版本时间线](/api/versions);注释覆盖情况见[覆盖率看板](/api/coverage)。')
  lines.push('')

  // 每组只归第一个匹配的分区;分区说明保留为 md 文字,分组列表交给
  // ApiGroupExplorer 组件(卡片矩阵 + 即时筛选),数据内联 props
  const byId = new Map(bundle.symbols.map(s => [s.id, s]))
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
  }
  const cards = bundle.groups.map(g => {
    const samples = g.symbolIds
      .map(id => byId.get(id))
      .filter((s): s is ApiSymbol => !!s && s.kind === 'function' && s.documented)
      .slice(0, 3)
      .map(s => s.name)
    const cov = bundle.coverage.find(c => c.group === g.name)
    return {
      name: g.name,
      title: g.title,
      section: sectionOf.get(g.name)!.title,
      total: g.symbolIds.length,
      withBrief: cov?.withBrief ?? 0,
      samples,
    }
  })
  lines.push(`<ApiGroupExplorer :groups='${JSON.stringify(cards).replace(/'/g, "&#39;")}' />`)
  lines.push('')
  return lines.join('\n')
}

export interface PageEmitOptions {
  outDir: string
  repo: { owner: string, repo: string, branch: string }
}

/** @since 版本时间线页:每个版本一节,列该版本引入的符号(按组聚合) */
function versionsPage(bundle: ApiBundle): string {
  const byVersion = new Map<string, ApiSymbol[]>()
  for (const s of bundle.symbols) {
    if (!s.since) continue
    const list = byVersion.get(s.since) ?? []
    list.push(s)
    byVersion.set(s.since, list)
  }
  // 版本排序:语义化版本降序(新在前);非标准版本号排最后
  const versionKey = (v: string): number[] => {
    const parts = v.split('.').map(x => parseInt(x, 10))
    return parts.every(n => !isNaN(n)) ? parts : [0]
  }
  const versions = Array.from(byVersion.keys()).sort((a, b) => {
    const ka = versionKey(a)
    const kb = versionKey(b)
    for (let i = 0; i < Math.max(ka.length, kb.length); i++) {
      const da = ka[i] ?? 0
      const db = kb[i] ?? 0
      if (da !== db) return db - da
    }
    return a.localeCompare(b)
  })

  const groupOf = new Map<string, string>()
  for (const g of bundle.groups) {
    for (const id of g.symbolIds) groupOf.set(id, g.title)
  }

  const lines: string[] = []
  lines.push('---')
  lines.push('title: 版本时间线')
  lines.push('---')
  lines.push('')
  lines.push('# 版本时间线')
  lines.push('')
  const documented = bundle.symbols.filter(s => s.since).length
  lines.push(
    `${documented} 个符号标注了 \`@since\`,按引入版本从新到旧排列。版本对应关系来自源码注释的声明,与发布记录互为参照。`,
  )
  lines.push('')
  lines.push('| 版本 | 符号数 |')
  lines.push('|---|---|')
  for (const v of versions) {
    lines.push(`| [${v}](#${v.replace(/\./g, '')}) | ${byVersion.get(v)!.length} |`)
  }
  lines.push('')
  for (const v of versions) {
    lines.push(`## ${v} {#${v.replace(/\./g, '')}}`)
    lines.push('')
    // 按组聚合,组内列符号名
    const perGroup = new Map<string, string[]>()
    for (const s of byVersion.get(v)!) {
      const g = groupOf.get(s.id) ?? '未分组'
      const list = perGroup.get(g) ?? []
      list.push(s.name)
      perGroup.set(g, list)
    }
    for (const [g, names] of Array.from(perGroup.entries()).sort((a, b) => a[0].localeCompare(b[0]))) {
      lines.push(`- **${g}** — ${names.sort((a, b) => a.localeCompare(b)).map(n => `\`${n}\``).join(' ')}`)
    }
    lines.push('')
  }
  return lines.join('\n')
}

/** 覆盖率看板:按 brief 覆盖率升序,缺注释最多的组排最前 */
function coveragePage(bundle: ApiBundle): string {
  const rows = [...bundle.coverage].sort((a, b) => {
    const pa = a.total === 0 ? 1 : a.withBrief / a.total
    const pb = b.total === 0 ? 1 : b.withBrief / b.total
    return pa - pb
  })
  const badge = (n: number, d: number): string => {
    const pct = d === 0 ? 100 : Math.round((n / d) * 100)
    const type = pct >= 95 ? 'tip' : pct >= 70 ? 'info' : 'warning'
    return `<Badge type="${type}" text="${n}/${d}" />`
  }
  const lines: string[] = []
  lines.push('---')
  lines.push('title: 文档覆盖率')
  lines.push('---')
  lines.push('')
  lines.push('# 文档覆盖率')
  lines.push('')
  lines.push('按分组统计源码注释的完备度,缺注释最多的组排最前。`lint.py` 管注释格式,这张表管注释覆盖,两边互补。数据随 `pnpm docs:api` 更新。')
  lines.push('')
  lines.push('| 分组 | 符号 | @brief | @since | 参数说明 |')
  lines.push('|---|---|---|---|---|')
  for (const c of rows) {
    const paramCell = c.paramDocs ? badge(c.paramDocs.covered, c.paramDocs.total) : '—'
    lines.push(`| [${esc(c.title)}](/api/${c.group}/) | ${c.total} | ${badge(c.withBrief, c.total)} | ${badge(c.withSince, c.total)} | ${paramCell} |`)
  }
  lines.push('')
  return lines.join('\n')
}

export function emitPages(bundle: ApiBundle, opts: PageEmitOptions): string[] {
  // 清掉旧页(组名改名后会留孤儿)
  rmSync(opts.outDir, { recursive: true, force: true })
  mkdirSync(opts.outDir, { recursive: true })

  const written: string[] = []
  const ordered = bundle.groups
  ordered.forEach((group, i) => {
    const dir = join(opts.outDir, group.name)
    mkdirSync(dir, { recursive: true })
    const file = join(dir, 'index.md')
    const prev = i > 0 ? { title: ordered[i - 1].title, name: ordered[i - 1].name } : undefined
    const next = i < ordered.length - 1 ? { title: ordered[i + 1].title, name: ordered[i + 1].name } : undefined
    writeFileSync(file, groupPage(group, bundle.symbols, bundle.coverage, opts.repo, { prev, next }))
    written.push(file)
  })
  const home = join(opts.outDir, 'index.md')
  writeFileSync(home, homePage(bundle))
  written.push(home)
  const versions = join(opts.outDir, 'versions.md')
  writeFileSync(versions, versionsPage(bundle))
  written.push(versions)
  const coverage = join(opts.outDir, 'coverage.md')
  writeFileSync(coverage, coveragePage(bundle))
  written.push(coverage)
  console.log(`pages: ${written.length} 个页面 → ${opts.outDir} (现有 ${readdirSync(opts.outDir).length} 项)`)
  return written
}
