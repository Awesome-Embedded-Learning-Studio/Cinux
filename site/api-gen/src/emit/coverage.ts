// 覆盖率统计:按 @ingroup 组聚合注释完备度。
//
// 分母是组内符号总数;分子统计有 brief / 有 since 的数量;参数说明
// 覆盖只对函数类符号计(有参数却零条参数描述算未覆盖)。

import type { ApiGroup, ApiSymbol, CoverageEntry } from '../model/symbol'

const FUNCTION_KINDS = new Set(['function', 'slot', 'signal'])

export function computeCoverage(groups: ApiGroup[], symbols: ApiSymbol[]): CoverageEntry[] {
  const byId = new Map(symbols.map(s => [s.id, s]))
  const out: CoverageEntry[] = []

  for (const group of groups) {
    let withBrief = 0
    let withSince = 0
    let paramCovered = 0
    let paramTotal = 0

    for (const id of group.symbolIds) {
      const s = byId.get(id)
      if (!s) continue
      if (s.brief) withBrief++
      if (s.since) withSince++
      if (FUNCTION_KINDS.has(s.kind)) {
        paramTotal++
        const hasParams = s.params.length > 0
        const allDescribed = s.params.every(p => p.description !== '')
        if (!hasParams || allDescribed) paramCovered++
      }
    }

    out.push({
      group: group.name,
      title: group.title,
      total: group.symbolIds.length,
      withBrief,
      withSince,
      paramDocs: paramTotal > 0 ? { covered: paramCovered, total: paramTotal } : null,
    })
  }
  return out
}
