// 日志卷链接改写:相对链接指向站外目标时改写为 GitHub URL。
//
// 背景:开发日志(317 篇)里有 391 条相对链接,其中 300+ 指向源码文件
// (../../kernel/xxx.cpp),另有一批指向不上站的内部目录(document/todo、
// document/ai)。这些目标在站点产物里不存在,相对链接会 404。
//
// 规则(按当前 md 的真实源文件位置 resolve):
//   - 落点在上站目录(notes/ci/document/design/changelogs/api)内 → 保持原样
//   - 其余(源码、todo、ai、ai_prompts、任何 document/ 外目标)→
//     https://github.com/<owner>/<repo>/blob/main/<仓库相对路径>#L<行号>
//
// 分卷构建把 md 拷进临时目录,env.path 仍是「卷内相对路径」(如
// notes/2026-x.md),与源文件 document/notes/2026-x.md 一一对应,
// 两种构建下解析结果一致。

import type { PluginSimple } from 'markdown-it'
import type MarkdownIt from 'markdown-it'
import { resolve, dirname, relative } from 'node:path'
import { statSync } from 'node:fs'

function fileExists(p: string): boolean {
  try {
    return statSync(p).isFile()
  } catch {
    return false
  }
}

export interface SourceLinkOptions {
  owner: string
  repo: string
  branch: string
  /** 站点根的 documents 目录名(相对仓库根) */
  documentsDir: string
}

/** 上站目录(站内有页面) */
const PUBLISHED_DIRS = ['notes', 'ci', 'document', 'design', 'changelogs', 'api']

function normalizeSlashes(p: string): string {
  return p.replace(/\\/g, '/')
}

export function sourceLinkPlugin(md: MarkdownIt, opts: SourceLinkOptions): void {
  const githubBase = `https://github.com/${opts.owner}/${opts.repo}/blob/${opts.branch}`

  md.core.ruler.after('inline', 'source_link_rewrite', (state) => {
    // env.path 是绝对路径,分卷构建时指向临时副本;.relativePath 才是
    // 卷内相对路径(notes/xxx.md),与源文件 document/notes/xxx.md 对应
    const envRel: string | undefined = (state.env as { relativePath?: string }).relativePath
    if (!envRel) return
    if (!envRel.startsWith('notes/')) return   // 只处理日志卷,其他卷无此问题

    const srcFileAbs = resolve(process.cwd(), opts.documentsDir, normalizeSlashes(envRel))

    for (const token of state.tokens) {
      if (!token.children) continue
      // 伪链接剥壳时的配对 depth:link_open 置 >0,直到配对的 link_close
      let stripDepth = 0
      for (let i = 0; i < token.children.length; i++) {
        const t: any = token.children[i]

        if (stripDepth > 0 && t.type === 'link_close') {
          stripDepth--
          t.type = 'html_inline'
          t.content = '</span>'
          continue
        }
        if (stripDepth > 0 && t.type === 'link_open') {
          // 嵌套链接(罕见):同样剥壳
          stripDepth++
          t.type = 'html_inline'
          t.content = '<span>'
          t.attrs = null
          continue
        }

        if (!t.attrs) continue
        const hrefIdx = t.attrs.findIndex((a: string[]) => a[0] === 'href')
        if (hrefIdx < 0) continue
        const href: string = t.attrs[hrefIdx][1]
        // 相对路径的统一判定:不以锚点/协议/站内绝对路径开头
        // (涵盖 ./x、../x 和裸相对 x 三种写法)
        if (/^([#a-zA-Z][a-zA-Z0-9+.-]*:|\/)/.test(href)) continue

        const hashIdx = href.indexOf('#')
        const pathPart = hashIdx >= 0 ? href.slice(0, hashIdx) : href
        const hashPart = hashIdx >= 0 ? href.slice(hashIdx) : ''
        if (!pathPart) continue

        let resolvedAbs = resolve(dirname(srcFileAbs), decodeURIComponent(pathPart))
        // 裸相对路径按仓库根坐标写的场景:源文件坐标解析不到、仓库根能
        // 命中真实文件时,以仓库根为基准改写(kernel/syscall/x.cpp 这类)
        if (!fileExists(resolvedAbs)) {
          const fromRoot = resolve(process.cwd(), decodeURIComponent(pathPart))
          if (fileExists(fromRoot)) {
            resolvedAbs = fromRoot
          } else {
            // 两坐标都解析不到:这是「术语(注释)」被 markdown 误吃的伪链接
            // (如 base_[4](0x10/4)、字段(未初始化))。剥掉链接壳保留文字。
            stripDepth = 1
            t.type = 'html_inline'
            t.content = '<span>'
            t.attrs = null
            continue
          }
        }
        const repoRel = normalizeSlashes(relative(process.cwd(), resolvedAbs))
        if (repoRel.startsWith('..')) {
          // 仓库外目标同样按伪链接剥壳
          stripDepth = 1
          t.type = 'html_inline'
          t.content = '<span>'
          t.attrs = null
          continue
        }

        const inDocuments = repoRel.startsWith(`${opts.documentsDir}/`)
        const docSub = inDocuments ? repoRel.slice(opts.documentsDir.length + 1) : ''
        const firstSeg = docSub.split('/')[0] ?? ''
        const published = inDocuments && PUBLISHED_DIRS.includes(firstSeg)

        if (!published) {
          t.attrs[hrefIdx][1] = `${githubBase}/${repoRel}${hashPart}`
        }
      }
    }
    return true
  })
}

