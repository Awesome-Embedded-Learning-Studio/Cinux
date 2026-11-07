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
  for (const s of symbols) {
    const memberships = extraMembership.get(s.id)
    if (memberships) {
      for (const name of memberships) {
        if (!s.groups.includes(name)) s.groups.push(name)
      }
    }
    s.groups.sort()
  }

  const coverage = computeCoverage(groups, symbols)

  const groupOf = new Map<string, string>()
  for (const g of groups) {
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
    groups,
    coverage,
    searchIndex,
    meta: {
      generatedAt: new Date().toISOString(),
      symbolCount: symbols.length,
      groupCount: groups.length,
    },
  }
}
