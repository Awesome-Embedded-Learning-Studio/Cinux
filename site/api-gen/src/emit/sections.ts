// 非函数符号的小节渲染:变量表、枚举与类型别名的 markdown 小节。
//
// 从 pages.ts 抽出来,供普通组页与富组页共用,避免 emit 层互相 import
// 形成环。

import type { ApiSymbol } from '../model/symbol'
import { anchorOf, githubLink, type ApiRepo } from './links'

export const KIND_ORDER = ['function', 'variable', 'enum', 'typedef']

export const KIND_TITLES: Record<string, string> = {
  function: '函数',
  variable: '变量',
  enum: '枚举',
  typedef: '类型别名',
  friend: '友元',
}

/** md 表格与正文里的管道符转义 */
export function esc(text: string): string {
  return text.replace(/\|/g, '\\|')
}

export function formatParams(s: ApiSymbol): string {
  if (s.params.length === 0) return ''
  const hasDesc = s.params.some(p => p.description)
  if (!hasDesc) return ''
  const dirMark = (d?: string) => (d ? `\`${d}\`` : '')
  const rows = s.params
    .map(p => `| \`${esc(p.name)}\` | ${dirMark(p.direction)} | ${esc(p.description)} |`)
    .join('\n')
  return `| 参数 | 方向 | 说明 |\n|---|---|---|\n${rows}\n`
}

export function symbolSection(s: ApiSymbol, repo: ApiRepo): string {
  const parts: string[] = []
  parts.push(`#### ${esc(s.qualifiedName)} {#${anchorOf(s)}}`)
  parts.push('')
  parts.push('```cpp')
  // 枚举的原始签名只有底层类型加名字,读不出这是枚举,补上 enum 与底层类型
  parts.push(esc(s.kind === 'enum' && s.returnType ? `enum ${s.name} : ${s.returnType}` : s.signature))
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
  if (s.kind === 'enum' && s.values.length > 0) {
    parts.push('| 取值 | 说明 |')
    parts.push('| --- | --- |')
    for (const v of s.values) {
      const value = v.value ? ` ${esc(v.value)}` : ''
      parts.push(`| \`${esc(v.name)}\`${value} | ${esc(v.brief) || '—'} |`)
    }
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

export function variablesTable(vars: ApiSymbol[], repo: ApiRepo): string {
  const rows = vars
    .map(v => {
      const link = githubLink(v, repo)
      const src = link ? `[\`${v.location.file}:${v.location.line}\`](${link})` : ''
      const since = v.since ? `\`${v.since}\`` : ''
      const brief = v.brief || '—'
      const anchor = `<span id="${anchorOf(v)}"></span>`
      return `| ${anchor}\`${esc(v.name)}\` | \`${esc(v.returnType)}\` | ${esc(brief)} | ${since || '—'} | ${src} |`
    })
    .join('\n')
  return `| 名称 | 类型 | 说明 | 自 | 源码 |\n|---|---|---|---|---|\n${rows}\n`
}
