// 开发日志索引页生成:document/notes/index.md。
//
// 按域分组列出全部日志(标题 + 链接),域内按文件名(日期)倒序——
// 新的在前。跑法:pnpm docs:notes(内容变了重跑一次)。

import { readFileSync, readdirSync, writeFileSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { NOTE_DOMAINS, TOPIC_GROUP_TITLE, domainOf, noteTitle } from '../site/.vitepress/config/notes-domains'

const PROJECT_ROOT = resolve(import.meta.dirname, '..')
const NOTES_DIR = join(PROJECT_ROOT, 'document/notes')

function h1Of(file: string): string | null {
  try {
    const m = readFileSync(join(NOTES_DIR, file), 'utf-8').match(/^#\s+(.+)$/m)
    return m ? m[1].trim() : null
  } catch {
    return null
  }
}

const files = readdirSync(NOTES_DIR)
  .filter(f => f.endsWith('.md') && f !== 'index.md')
  .sort()
  .reverse() // 新的在前

const buckets = new Map<string, { title: string, files: string[] }>()
for (const d of NOTE_DOMAINS) buckets.set(d.id, { title: d.title, files: [] })
buckets.set('topic', { title: TOPIC_GROUP_TITLE, files: [] })

for (const f of files) {
  const dom = domainOf(f)
  buckets.get(dom ? dom.id : 'topic')!.files.push(f)
}

const lines: string[] = [
  '---',
  'title: 开发日志',
  '---',
  '',
  '# 开发日志',
  '',
  `${files.length} 篇研发过程记录,按功能域分组,域内新的在前。这些是当时的现场记录,描述的实现在后来可能已经改过;当前行为以[系统文档](/systems/)为准。`,
  '',
  '源码链接跳 GitHub,仓库内部的规划文档(todo/ai)不在本站范围。',
  '',
]

for (const [id, b] of buckets) {
  if (b.files.length === 0) continue
  lines.push(`## ${b.title}(${b.files.length})`)
  lines.push('')
  for (const f of b.files) {
    const date = f.slice(0, 10)
    lines.push(`- ${date} [${noteTitle(f, h1Of(f))}](/notes/${f.replace(/\.md$/, '')})`)
  }
  lines.push('')
}

writeFileSync(join(NOTES_DIR, 'index.md'), lines.join('\n'))
console.log(`notes index: ${files.length} 篇 → document/notes/index.md`)
