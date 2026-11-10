// 富组页生成:把数学页验证过的阅读结构推广到其余分组。
//
// 页面骨架仍然是 markdown——标题、代码、表格照常进 VitePress 搜索和
// 静态输出;交互部分叠在骨架上:模块页头、用途索引、函数卡片。
// 中文导读从 guides/<组名>.json 读;没有导读数据时回退到源码注释,
// 结构不变,只是文案还是英文原文。

import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import type { ApiGroup, ApiSymbol, CoverageEntry } from '../model/symbol'
import { anchorOf, githubLink, type ApiRepo } from './links'
import { KIND_ORDER, KIND_TITLES, esc, formatParams, symbolSection, variablesTable } from './sections'

export interface GuideFunction {
  /** 中文一句话,替代源码里的英文 @brief 出现在卡片头部 */
  summary: string
  /** 用途分类,必须属于 guide.categories */
  category: string
  /** 可选的使用示例,当前批次不填 */
  example?: string
}

export interface GroupGuide {
  group: string
  /** 组内符号的公共命名空间,用于页头展示 */
  namespace?: string
  /** 页头导语,一句话 */
  intro?: string
  /** 用途分类,顺序即索引按钮顺序 */
  categories?: string[]
  /** 键为函数名,同名冲突时用完整限定名 */
  functions?: Record<string, GuideFunction>
  /** 组页内嵌的交互组件,按顺序插在用途索引之后 */
  components?: GuideComponent[]
}

export interface GuideComponent {
  /** 小节锚点,写进 h2 的 {#id} */
  anchor: string
  /** 小节标题 */
  title: string
  /** 在 theme/index.ts 注册的组件名 */
  component: string
  /** 小节下方的一句说明 */
  note?: string
}

const GUIDE_DIR = join(import.meta.dirname, 'guides')
const guideCache = new Map<string, GroupGuide | null>()

export function loadGuide(name: string): GroupGuide | null {
  if (guideCache.has(name)) return guideCache.get(name)!
  let guide: GroupGuide | null = null
  const file = join(GUIDE_DIR, `${name}.json`)
  if (existsSync(file)) {
    try {
      guide = JSON.parse(readFileSync(file, 'utf8')) as GroupGuide
    } catch (err) {
      console.warn(`guide ${name}: JSON 解析失败,回退到源码注释 — ${(err as Error).message}`)
    }
  }
  guideCache.set(name, guide)
  return guide
}

/** 去掉键尾的参数表与 const 限定,重载函数因此能落到同一个前缀上 */
function normalizeKey(key: string): string {
  const open = key.indexOf('(')
  return open >= 0 ? key.slice(0, open) : key
}

/** 从原始键里取参数表原文,用于在重载之间比对签名 */
function paramsOf(key: string): string {
  const open = key.indexOf('(')
  if (open < 0) return ''
  const close = key.lastIndexOf(')')
  return close > open ? key.slice(open + 1, close).replace(/\s+/g, ' ').trim() : ''
}

function paramScore(key: string, signature: string): number {
  const params = paramsOf(key)
  const flat = signature.replace(/\s+/g, '')
  // const 限定也是重载的一部分:末尾带 const 的签名只认末尾带 const 的键
  const keyConst = /\)\s*const\s*$/.test(key)
  const sigConst = /const$/.test(signature.trim())
  let score = keyConst === sigConst ? 1 : 0
  if (params) {
    score += params.split(',').filter(p => {
      const t = p.replace(/\s+/g, '')
      return t.length > 0 && flat.includes(t)
    }).length
  }
  return score
}

type GuideIndex = Map<string, { entry: GuideFunction, key: string }[]>

const indexCache = new WeakMap<GroupGuide, GuideIndex>()

function indexOf(guide: GroupGuide): GuideIndex {
  const cached = indexCache.get(guide)
  if (cached) return cached
  const index: GuideIndex = new Map()
  for (const [key, entry] of Object.entries(guide.functions ?? {})) {
    const norm = normalizeKey(key)
    const list = index.get(norm) ?? []
    list.push({ entry, key })
    index.set(norm, list)
  }
  indexCache.set(guide, index)
  return index
}

/** 解析一个符号对应的导读条目:重名或重载时用参数表在签名里比对</summary> */
export function guideEntryOf(guide: GroupGuide | null, s: ApiSymbol): GuideFunction | undefined {
  const table = guide?.functions
  if (!table) return undefined
  const direct = table[s.name] ?? table[s.qualifiedName]
  const index = indexOf(guide!)
  const lookup = (key: string) => index.get(normalizeKey(key))
  for (const key of [s.name, s.qualifiedName]) {
    const hits = lookup(key)
    if (!hits || hits.length === 0) continue
    if (hits.length === 1) return hits[0].entry
    let best = hits[0]
    let bestScore = -1
    for (const hit of hits) {
      const score = paramScore(hit.key, s.signature)
      if (score > bestScore) {
        best = hit
        bestScore = score
      }
    }
    return best.entry
  }
  return direct
}

function attr(value: unknown): string {
  return JSON.stringify(value).replace(/&/g, '&amp;').replace(/'/g, '&#39;')
    .replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

function text(value: string): string {
  return value.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/\{/g, '&#123;').replace(/\}/g, '&#125;').replace(/\|/g, '\\|')
}

function fence(code: string): string {
  return `\`\`\`cpp:no-line-numbers\n${code}\n\`\`\``
}

/** 公共命名空间:去掉每个符号最后一段名字后,取最长公共前缀。
 *  全局作用域的函数没有命名空间,让它参与求交会把结果压成空串,
 *  页头因此只剩一个空位,所以先排除没有命名空间的符号。 */
export function commonNamespace(members: ApiSymbol[]): string {
  const splits = members
    .filter(m => m.qualifiedName.includes('::'))
    .map(m => m.qualifiedName.split('::').slice(0, -1))
  if (splits.length === 0) return ''
  const first = splits[0]
  let len = first.length
  for (const segs of splits) {
    let i = 0
    while (i < len && i < segs.length && segs[i] === first[i]) i++
    len = i
  }
  return first.slice(0, len).join('::')
}

/** 符号所属容器:命名空间之内的类名或结构名,自由函数返回空串 */
function ownerOf(s: ApiSymbol, namespace: string): string {
  const parts = s.qualifiedName.split('::')
  const nsParts = namespace ? namespace.split('::') : []
  const owners = parts.slice(nsParts.length, -1)
  return owners.join('::')
}

type Neighbor = { title: string, name: string }

export function richGroupPage(
  group: ApiGroup,
  members: ApiSymbol[],
  symbols: ApiSymbol[],
  coverage: CoverageEntry | undefined,
  repo: ApiRepo,
  neighbors?: { prev?: Neighbor, next?: Neighbor },
): string {
  const guide = loadGuide(group.name)
  const byId = new Map(symbols.map(s => [s.id, s]))
  const functions = members.filter(m => m.kind === 'function')
  const others = members.filter(m => m.kind !== 'function')
  const namespace = guide?.namespace || commonNamespace(functions.length > 0 ? functions : members)
  const namespaceLabel = namespace || '全局作用域'

  const entryOf = (s: ApiSymbol): GuideFunction | undefined => guideEntryOf(guide, s)

  const categoryOrder = new Map((guide?.categories ?? []).map((c, i) => [c, i]))
  const categoryOf = (s: ApiSymbol): string =>
    entryOf(s)?.category || ownerOf(s, namespace) || '全局函数'
  const summaryOf = (s: ApiSymbol): string => entryOf(s)?.summary || s.brief || ''
  // 卡片头部总要有一行字:源码缺 @brief 时说明缺的是什么,不留空白
  const displaySummary = (s: ApiSymbol): string => summaryOf(s) || '源码注释未提供说明'

  // 索引顺序跟随导读里的分类顺序,同类的按名字排;没有导读时按分类名排,
  // 读者仍然先看到同一类接口挨在一起
  const orderedFunctions = [...functions].sort((a, b) => {
    const ca = categoryOf(a)
    const cb = categoryOf(b)
    const ra = categoryOrder.get(ca) ?? Number.MAX_SAFE_INTEGER
    const rb = categoryOrder.get(cb) ?? Number.MAX_SAFE_INTEGER
    if (ra !== rb) return ra - rb
    if (ca !== cb) return ca.localeCompare(cb)
    return a.name.localeCompare(b.name)
  })

  const items = orderedFunctions.map(s => ({
    name: s.name,
    anchor: anchorOf(s),
    summary: displaySummary(s),
    category: categoryOf(s),
    kind: 'function',
  }))
  // 非函数符号也进索引:变量、枚举、类型别名在页面底部各有落点,
  // 只列函数会让读者从速览里找不到它们,深链也只能靠手拼。
  const otherItems = others.map(s => ({
    name: s.name,
    anchor: anchorOf(s),
    summary: s.brief,
    category: KIND_TITLES[s.kind] ?? '其他',
    kind: s.kind,
  }))

  const documented = functions.filter(s => !!summaryOf(s)).length
  const headerOnly = members.length > 0 && members.every(m => /\.(hpp|h)$/.test(m.location.file))
  const properties = headerOnly ? ['header-only'] : []

  const intro = guide?.intro
    || `本组收录 ${functions.length} 个函数${others.length > 0 ? `、${others.length} 个其他符号` : ''},签名与参数由源码注释生成。`

  // 目录的层级过滤只决定从哪一层起算,更深的标题会整棵挂在最后一个标题下,
  // 244 个函数就是 244 条右侧链接。函数多到一定规模时改用顶部用途索引导航。
  const outline = functions.length > 40 ? 'false' : '[2, 3]'
  const lines: string[] = [
    '---',
    `title: ${group.title}`,
    'pageClass: api-rich-page',
    `outline: ${outline}`,
    '---', '',
  ]

  lines.push(
    `<ApiModuleOverview namespace="${text(namespaceLabel)}" :count='${attr(functions.length)}' :documented='${attr(documented)}'${properties.length > 0 ? ` :properties='${attr(properties)}'` : ''}>`,
    '',
    `# ${group.title}`,
    '',
    intro,
    '',
    `\`@ingroup ${group.name}\``,
    '',
    '</ApiModuleOverview>', '',
  )

  if (functions.length > 0) {
    lines.push('## 接口速览 {#overview}', '', `<ApiFunctionIndex :items='${attr([...items, ...otherItems])}' />`, '')
  }

  for (const block of guide?.components ?? []) {
    lines.push(`## ${text(block.title)} {#${block.anchor}}`, '', `<${block.component} />`, '')
    if (block.note) lines.push(`> ${text(block.note)}`, '')
  }

  if (orderedFunctions.length > 0) {
    lines.push('## 接口参考 {#reference}', '')
  }

  for (const s of orderedFunctions) {
    const entry = entryOf(s)
    const seen = new Set([s.id])
    const callers = s.referencedBy.filter(r => {
      if (seen.has(r.id)) return false
      seen.add(r.id)
      return true
    }).map(r => {
      const target = byId.get(r.id)
      const targetGroup = target?.groups.find(g => g === group.name) || target?.groups[0]
      return {
        name: r.name,
        href: target && targetGroup ? `/api/${targetGroup}/#${anchorOf(target)}` : '',
      }
    })
    const owner = ownerOf(s, namespace)
    const meta = {
      name: s.name,
      anchor: anchorOf(s),
      namespace,
      qualifier: '',
      since: s.since,
      source: githubLink(s, repo),
      sourceLabel: `${s.location.file}:${s.location.line}`,
      summary: displaySummary(s),
      callers,
    }
    const heading = owner ? `${owner}::${s.name}` : s.name

    lines.push(`<ApiFunctionCard v-bind='${attr(meta)}'>`, '',
      '<template #heading>', '',
      `### ${text(heading)} {#${anchorOf(s)}}`, '', '</template>', '',
      '<template #declaration>', '', fence(s.signature.replace(s.qualifiedName, s.name)), '', '</template>', '')

    if (entry?.example) {
      lines.push('<template #example>', '', fence(entry.example), '', '</template>', '')
    }

    // 卡片头部已经显示中文摘要,正文只放源码里的详情段,不重复一句话
    if (s.details) lines.push(text(s.details), '')

    if (s.params.length) {
      lines.push('<template #parameters>', '', '| 参数 | 类型 | 说明 |', '| --- | --- | --- |')
      for (const p of s.params) {
        const dir = p.direction ? ` <span class="api-param-direction">${text(p.direction)}</span>` : ''
        lines.push(`| \`${text(p.name)}\`${dir} | \`${text(p.type)}\` | ${text(p.description)} |`)
      }
      lines.push('', '</template>', '')
    }
    if (s.returns && !/^(none|无)$/i.test(s.returns)) {
      lines.push('<template #returns>', '', text(s.returns), '', '</template>', '')
    }
    if (s.note && !/^(none|无)$/i.test(s.note)) {
      lines.push('<template #notes>', '', `> ${text(s.note)}`, '', '</template>', '')
    }
    if (s.warning && !/^(none|无)$/i.test(s.warning)) {
      lines.push('<template #warning>', '', text(s.warning), '', '</template>', '')
    }
    lines.push('</ApiFunctionCard>', '')
  }

  // 非函数符号保持紧凑表/小节,和原来的组页一致
  const kinds = new Set(others.map(m => m.kind))
  for (const kind of KIND_ORDER.filter(k => kinds.has(k))) {
    const pool = others.filter(m => m.kind === kind)
    lines.push(`## ${KIND_TITLES[kind] ?? kind}`, '')
    if (kind === 'variable' || kind === 'typedef') {
      lines.push(variablesTable(pool, repo))
    } else {
      for (const s of pool) lines.push(symbolSection(s, repo))
    }
  }
  const rest = others.filter(m => !KIND_ORDER.includes(m.kind))
  if (rest.length > 0) {
    lines.push('## 其他', '')
    for (const s of rest) lines.push(symbolSection(s, repo))
  }

  if (neighbors?.prev || neighbors?.next) {
    lines.push('<nav class="api-module-nav" aria-label="相邻 API 模块">', '')
    if (neighbors.prev) lines.push(`<a href="../${neighbors.prev.name}/"><span>← 上一模块</span><strong>${text(neighbors.prev.title)}</strong></a>`)
    if (neighbors.next) lines.push(`<a href="../${neighbors.next.name}/"><span>下一模块 →</span><strong>${text(neighbors.next.title)}</strong></a>`)
    lines.push('', '</nav>', '')
  }
  return lines.join('\n')
}
