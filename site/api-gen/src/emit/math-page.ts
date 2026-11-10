import type { ApiGroup, ApiSymbol, CoverageEntry } from '../model/symbol'
import { anchorOf, githubLink, type ApiRepo } from './links'

// 人工维护的导读与用例；签名、参数、版本和调用关系始终来自源码。
const GUIDE: Record<string, { summary: string, category: string, example: string }> = {
  AlignDown: {
    summary: '向下对齐，找到当前地址所在单元的起点。', category: '内存对齐',
    example: 'constexpr auto page = AlignDown(4097, 4096);\n// page = 4096 · 所在页的起始地址\n\nstatic_assert(AlignDown(8192, 4096) == 8192);',
  },
  AlignUp: {
    summary: '向上对齐，为数据预留足够的整单元空间。', category: '内存对齐',
    example: 'constexpr auto bytes = AlignUp(4097, 4096);\n// bytes = 8192 · 需要两个完整页\n\nstatic_assert(AlignUp(8192, 4096) == 8192);',
  },
  Ceil: {
    summary: '向上取整，计算容纳数值需要多少个单元。', category: '计数与区间',
    example: 'constexpr auto pages = Ceil(4097, 4096);\n// pages = 2 · 返回单元数量\n\nstatic_assert(Ceil(8192, 4096) == 2);',
  },
  Clamp: {
    summary: '限制上下界，把数值收进一个闭区间。', category: '边界取值',
    example: 'constexpr auto bounded = Clamp(120, 0, 100);\n// bounded = 100 · 超过上界时返回上界\n\nstatic_assert(Clamp(42, 0, 100) == 42);',
  },
  Floor: {
    summary: '向下取整，计算数值包含多少个完整单元。', category: '计数与区间',
    example: 'constexpr auto pages = Floor(4097, 4096);\n// pages = 1 · 只计算完整单元\n\nstatic_assert(Floor(8192, 4096) == 2);',
  },
  Max: {
    summary: '取两者中的较大值。', category: '边界取值',
    example: 'constexpr auto capacity = Max(512, 4096);\n// capacity = 4096\n\nstatic_assert(Max(42, 42) == 42);',
  },
  Min: {
    summary: '取两者中的较小值。', category: '边界取值',
    example: 'constexpr auto readable = Min(4096, 512);\n// readable = 512\n\nstatic_assert(Min(42, 42) == 42);',
  },
  Span: {
    summary: '计算覆盖半开区间 [base, top) 的单元数。', category: '计数与区间',
    example: 'constexpr auto pages = Span(4095, 4097, 4096);\n// pages = 2 · 区间跨过了页边界\n\nstatic_assert(Span(4096, 4096, 4096) == 0);',
  },
}

// 使用 JSON 表达式传 props，避免引号与 HTML 字符破坏组件属性。
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

type Neighbor = { title: string, name: string }

export function mathGroupPage(
  group: ApiGroup,
  members: ApiSymbol[],
  symbols: ApiSymbol[],
  coverage: CoverageEntry | undefined,
  repo: ApiRepo,
  neighbors?: { prev?: Neighbor, next?: Neighbor },
): string {
  const byId = new Map(symbols.map(s => [s.id, s]))
  const items = members.map(s => ({
    name: s.name, anchor: anchorOf(s),
    summary: GUIDE[s.name]?.summary || s.brief,
    category: GUIDE[s.name]?.category || '其他',
  }))
  const lines = [
    '---', `title: ${group.title}`, 'pageClass: api-rich-page', 'outline: [2, 3]', '---', '',
    `<ApiModuleOverview namespace="cinux::base::math" :properties='["header-only", "constexpr"]' :count='${attr(members.length)}' :documented='${attr(coverage?.withBrief ?? 0)}'>`, '',
    '# 数学 <span class="api-module__english">Math</span>', '',
    '从一个数值，到恰好的边界。对齐地址、约束范围，或计算跨过的每一页。', '',
    '</ApiModuleOverview>', '',
    '## 函数速览 {#overview}', '',
    `<ApiFunctionIndex :items='${attr(items)}' />`, '',
    '## 对齐实验室 {#playground}', '',
    '<ApiMathPlayground />', '',
    '## 接口参考 {#reference}', '',
  ]

  for (const s of members) {
    const guide = GUIDE[s.name]
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
    const meta = {
      name: s.name, anchor: anchorOf(s), namespace: s.qualifiedName.slice(0, s.qualifiedName.lastIndexOf('::')),
      qualifier: 'constexpr',
      since: s.since, source: githubLink(s, repo),
      sourceLabel: `${s.location.file}:${s.location.line}`,
      summary: guide?.summary || s.brief, callers,
    }
    lines.push(`<ApiFunctionCard v-bind='${attr(meta)}'>`, '', '<template #heading>', '',
      `### ${text(s.name)} {#${anchorOf(s)}}`, '', '</template>', '',
      '<template #declaration>', '', fence(s.signature.replace(s.qualifiedName, s.name)), '', '</template>', '')
    if (guide) {
      lines.push('<template #example>', '',
        fence(`#include <cinux/math.hpp>\nusing namespace cinux::base::math;\n\n${guide.example}`), '', '</template>', '')
    }
    if (s.brief) lines.push(text(s.brief), '')
    if (s.details) lines.push(text(s.details), '')
    if (s.params.length) {
      lines.push('<template #parameters>', '', '| 参数 | 类型 | 说明 |', '| --- | --- | --- |')
      for (const p of s.params) {
        lines.push(`| \`${text(p.name)}\` <span class="api-param-direction">${text(p.direction || '')}</span> | \`${text(p.type)}\` | ${text(p.description)} |`)
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
  if (neighbors?.prev || neighbors?.next) {
    lines.push('<nav class="api-module-nav" aria-label="相邻 API 模块">', '')
    if (neighbors.prev) lines.push(`<a href="../${neighbors.prev.name}/"><span>← 上一模块</span><strong>${text(neighbors.prev.title)}</strong></a>`)
    if (neighbors.next) lines.push(`<a href="../${neighbors.next.name}/"><span>下一模块 →</span><strong>${text(neighbors.next.title)}</strong></a>`)
    lines.push('', '</nav>', '')
  }
  return lines.join('\n')
}
