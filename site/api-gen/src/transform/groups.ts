// 分组转换:group compound → ApiGroup[]。
//
// 组成员的 refid 就是符号 id(组前缀),直接建列表;标题取 @defgroup
// 存根里的 title(generate-groups.ts 维护)。
// 一个符号可能 @ingroup 多个组:doxygen 会把它复制进每个组文件,
// refid 以第一次出现的组为前缀,但每个组文件都列它,这里照单全收。

import { textOf } from '../parse/xml'
import type { DoxyCompoundDef } from '../parse/doxygen'
import type { ApiGroup, ApiSymbol } from '../model/symbol'

export function groupFromCompound(
  def: DoxyCompoundDef,
  symbols: ApiSymbol[],
): ApiGroup | null {
  if (def['@kind'] !== 'group') return null
  const name = textOf(def.compoundname)
  if (!name) return null

  const byId = new Map(symbols.map(s => [s.id, s]))
  const ids: string[] = []

  // sectiondef/memberdef:组自己收编的符号(refid 即符号 id)
  for (const section of [def.sectiondef].flat()) {
    if (!section || typeof section !== 'object') continue
    const sec = section as Record<string, unknown>
    for (const member of [sec.memberdef].flat()) {
      if (!member || typeof member !== 'object') continue
      const id = (member as Record<string, unknown>)['@id']
      if (typeof id === 'string' && byId.has(id)) ids.push(id)
    }
  }

  return {
    name,
    title: textOf(def.title) || name,
    symbolIds: ids.sort((a, b) => {
      const sa = byId.get(a)?.qualifiedName ?? ''
      const sb = byId.get(b)?.qualifiedName ?? ''
      return sa === sb ? a.localeCompare(b) : sa.localeCompare(sb)
    }),
  }
}
