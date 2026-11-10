// 导读校验:核对 guides/*.json 与生成数据是否还对得上。
//
// 导读按函数名匹配,函数改名或签名变化后会静默退回英文摘要,页面看着
// 照常生成。这个脚本把没有命中导读的函数按组打出来,失败的组退出码非零。
//
// 用法:pnpm docs:api 之后跑 pnpm check:guides
//   --verbose  逐条列出缺失的函数名

import { readFileSync, existsSync } from 'node:fs'
import { resolve } from 'node:path'
import type { ApiBundle, ApiSymbol } from '../site/api-gen/src/model/symbol'
import { guideEntryOf, loadGuide } from '../site/api-gen/src/emit/group-rich'

const PROJECT_ROOT = resolve(import.meta.dirname, '..')
const BUNDLE = resolve(PROJECT_ROOT, 'site/api-gen/dist/api-bundle.json')
const verbose = process.argv.includes('--verbose')

if (!existsSync(BUNDLE)) {
  console.error(`找不到生成数据 ${BUNDLE},先跑 pnpm docs:api`)
  process.exit(1)
}

const bundle = JSON.parse(readFileSync(BUNDLE, 'utf8')) as ApiBundle
const byId = new Map(bundle.symbols.map(s => [s.id, s]))

let guidedGroups = 0
let guidedFunctions = 0
let missedFunctions = 0
let categoryErrors = 0
const failures: string[] = []

for (const group of bundle.groups) {
  const guide = loadGuide(group.name)
  if (!guide) continue
  guidedGroups++

  const members = group.symbolIds.map(id => byId.get(id)).filter((s): s is ApiSymbol => !!s)
  const functions = members.filter(s => s.kind === 'function')
  const categories = new Set(guide.categories ?? [])
  const missed: string[] = []

  for (const s of functions) {
    const entry = guideEntryOf(guide, s)
    if (!entry) {
      missed.push(s.name)
      continue
    }
    guidedFunctions++
    if (categories.size > 0 && !categories.has(entry.category)) {
      categoryErrors++
      failures.push(`${group.name} ${s.name}: 分类“${entry.category}”不在 categories 里`)
    }
    if (!/[\u4e00-\u9fa5]/.test(entry.summary)) {
      failures.push(`${group.name} ${s.name}: 摘要没有中文`)
    }
  }

  if (missed.length > 0) {
    missedFunctions += missed.length
    const sample = missed.slice(0, 5).join('、')
    failures.push(`${group.name}: ${missed.length}/${functions.length} 个函数没有导读（${sample}${missed.length > 5 ? ' 等' : ''}）`)
    if (verbose) for (const name of missed) failures.push(`  - ${group.name} ${name}`)
  }

  console.log(`${group.name.padEnd(18)} 函数 ${String(functions.length).padStart(3)}  命中 ${String(functions.length - missed.length).padStart(3)}`)
}

console.log(`\n有导读的组 ${guidedGroups}，命中函数 ${guidedFunctions}，缺失 ${missedFunctions}，分类越界 ${categoryErrors}`)
if (failures.length > 0) {
  console.error(`\n${failures.join('\n')}`)
  process.exit(1)
}
