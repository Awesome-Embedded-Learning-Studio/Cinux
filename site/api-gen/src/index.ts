// 管线编排:XML 目录 → ApiBundle。
//
// 供 cli 与测试共用;目录读取可注入,fixture 测试喂内存实现。

import { readdirSync } from 'node:fs'
import { join } from 'node:path'
import type { ApiBundle, SearchEntry, ApiGroup } from './model/symbol'
import { collectSymbols, parseCompound } from './transform/symbols'
import { groupFromCompound } from './transform/groups'
import { computeCoverage } from './emit/coverage'

export interface PipelineInput {
  xmlDir: string
  projectRoot: string
  readdir?: (d: string) => string[]
}

export function runPipeline(input: PipelineInput): ApiBundle {
  const readdir = input.readdir ?? ((d: string) => readdirSync(d))
  const { symbols } = collectSymbols(input.xmlDir, input.projectRoot)

  // 组:从 group__*.xml 收集,再把符号的 groups 字段按组文件补全
  // (符号的 groups 字段只在它定义所在的那个组文件里带了一次)
  const groups: ApiGroup[] = []
  const extraMembership = new Map<string, Set<string>>()
  for (const entry of readdir(input.xmlDir)) {
    if (!entry.startsWith('group__') || !entry.endsWith('.xml')) continue
    const compound = parseCompound(join(input.xmlDir, entry), input.projectRoot)
    const def = compound?.doxygen?.compounddef
    if (!def) continue
    const g = groupFromCompound(def, symbols)
    if (!g) continue
    groups.push(g)
    for (const id of g.symbolIds) {
      if (!extraMembership.has(id)) extraMembership.set(id, new Set())
      extraMembership.get(id)!.add(g.name)
    }
  }
  groups.sort((a, b) => a.name.localeCompare(b.name))
  // 空组过滤:组名被 @ingroup 引用但符号未被收录(如宏/file 级成员,
  // 管线不收 file compound 的已知边界)。空组不出页面不进导航,
  // generate-groups 的扫描仍会列出组名,符号收录扩展后自然回来。
  const emptyGroups = groups.filter(g => g.symbolIds.length === 0).map(g => g.name)
  if (emptyGroups.length > 0) {
    console.warn(`groups: 空组过滤(符号未收录): ${emptyGroups.join(', ')}`)
  }
  for (const s of symbols) {
    const memberships = extraMembership.get(s.id)
    if (memberships) {
      for (const name of memberships) {
        if (!s.groups.includes(name)) s.groups.push(name)
      }
    }
    s.groups.sort()
  }

  // referencedBy 归一:refid 命中符号表的显示限定名,并按名排序去重
  const byIdAll = new Map(symbols.map(s => [s.id, s]))
  for (const s of symbols) {
    const seen = new Set<string>()
    const resolved: { id: string, name: string }[] = []
    for (const r of s.referencedBy) {
      const target = byIdAll.get(r.id)
      const name = target?.qualifiedName ?? r.name
      if (seen.has(name)) continue
      seen.add(name)
      resolved.push({ id: r.id, name })
    }
    resolved.sort((a, b) => a.name.localeCompare(b.name))
    s.referencedBy = resolved
  }

  const liveGroups = groups.filter(g => g.symbolIds.length > 0)
  const coverage = computeCoverage(liveGroups, symbols)

  const groupOf = new Map<string, string>()
  for (const g of liveGroups) {
    for (const id of g.symbolIds) groupOf.set(id, g.name)
  }
  const searchIndex: SearchEntry[] = symbols.map(s => ({
    id: s.id,
    name: s.name,
    qualifiedName: s.qualifiedName,
    brief: s.brief,
    kind: s.kind,
    group: groupOf.get(s.id) ?? '',
  }))

  return {
    symbols,
    groups: liveGroups,
    coverage,
    searchIndex,
    meta: {
      generatedAt: new Date().toISOString(),
      symbolCount: symbols.length,
      groupCount: liveGroups.length,
    },
  }
}
