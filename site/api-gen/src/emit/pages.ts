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
import { richGroupPage } from './group-rich'
import { esc } from './sections'
import { mathGroupPage } from './math-page'

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
  const byId = new Map(bundle.symbols.map(s => [s.id, s]))
  ordered.forEach((group, i) => {
    const dir = join(opts.outDir, group.name)
    mkdirSync(dir, { recursive: true })
    const file = join(dir, 'index.md')
    const members = group.symbolIds.map(id => byId.get(id)).filter((s): s is ApiSymbol => !!s)
    const prev = i > 0 ? { title: ordered[i - 1].title, name: ordered[i - 1].name } : undefined
    const next = i < ordered.length - 1 ? { title: ordered[i + 1].title, name: ordered[i + 1].name } : undefined
    const page = group.name === 'base_math'
      ? mathGroupPage(group, members, bundle.symbols, bundle.coverage.find(c => c.group === group.name), opts.repo, { prev, next })
      : richGroupPage(group, members, bundle.symbols, bundle.coverage.find(c => c.group === group.name), opts.repo, { prev, next })
    writeFileSync(file, page)
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
