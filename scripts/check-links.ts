// 站内链接检查
//
// 分卷构建的生成配置里 ignoreDeadLinks 是全开(true),断链不会让构建失败;
// 这个脚本补上那道门禁,以源码为准做检查(比扫产物快,也能报出源文件名和行号)。
//
// 检查范围:project.config 声明的每个卷(srcDir)下的 .md 文件。
// 判定规则:
//   - http(s)、mailto、页内锚点:跳过
//   - 以 / 开头:站内绝对路径,按 base 与各卷 urlPrefix 反查源文件
//   - 相对路径:相对当前文件解析,目录补 index.md,无扩展名补 .md
//   - 指向 document/ 树之外(例如 ../../kernel/x.cpp):判为失败,这类链接
//     在站内没有目标,应在构建期改写成 GitHub 地址或改成纯文本
//
// 用法:pnpm check:links(失败时退出码 1)

import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs'
import { dirname, join, posix, relative, resolve } from 'node:path'
import projectConfig from '../project.config'

const PROJECT_ROOT = resolve(import.meta.dirname, '..')
const DOCS = join(PROJECT_ROOT, projectConfig.documentsDir)
const BASE_NO_SLASH = projectConfig.base.replace(/\/$/, '')

/** 上站目录(与 source-link-plugin 的 PUBLISHED_DIRS 保持一致) */
const PUBLISHED_VOLUME_DIRS = ['notes', 'ci', 'document', 'design', 'changelogs', 'api']

interface Volume {
  srcDir: string
  urlPrefix: string
}

const volumes: Volume[] = projectConfig.sidebar.volumes.map(v => ({
  srcDir: v.srcDir,
  urlPrefix: v.urlPrefix,
}))

interface Failure {
  file: string
  line: number
  target: string
  reason: string
}

const failures: Failure[] = []
let checkedFiles = 0
let checkedLinks = 0

function walk(dir: string, out: string[] = []): string[] {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    if (e.name.startsWith('.')) continue
    const full = join(dir, e.name)
    if (e.isDirectory()) walk(full, out)
    else if (e.name.endsWith('.md')) out.push(full)
  }
  return out
}

/** 源文件 → 站内 URL 路径(不带 base) */
function fileToUrl(volume: Volume, file: string): string {
  const rel = relative(join(DOCS, volume.srcDir), file).replace(/\\/g, '/').replace(/\.md$/, '')
  if (rel === 'index') return `${volume.urlPrefix}/`
  if (rel.endsWith('/index')) return `${volume.urlPrefix}/${rel.slice(0, -'/index'.length)}/`
  return `${volume.urlPrefix}/${rel}`
}

/** 站内 URL(去掉 base) → 源文件绝对路径;找不到返回 null */
function urlToFile(urlPath: string): string | null {
  const clean = urlPath.replace(/\/$/, '') || '/'
  for (const volume of volumes) {
    const prefix = volume.urlPrefix
    if (clean === prefix) {
      const candidate = join(DOCS, volume.srcDir, 'index.md')
      return existsSync(candidate) ? candidate : null
    }
    if (clean.startsWith(`${prefix}/`)) {
      const rest = clean.slice(prefix.length + 1)
      const direct = join(DOCS, volume.srcDir, `${rest}.md`)
      if (existsSync(direct)) return direct
      const asIndex = join(DOCS, volume.srcDir, rest, 'index.md')
      if (existsSync(asIndex)) return asIndex
      return null
    }
  }
  return null
}

function stripDecorations(target: string): string {
  return target.replace(/[?#].*$/, '')
}

function isExternal(target: string): boolean {
  return /^(https?:)?\/\//i.test(target) || /^(mailto|tel|data|javascript):/i.test(target)
}

function checkTarget(file: string, line: number, rawTarget: string, isImage: boolean): void {
  checkedLinks++
  const target = rawTarget.trim()
  if (!target || target.startsWith('#')) return
  if (isExternal(target)) return

  const path = stripDecorations(target)
  if (!path) return

  // 站内绝对路径
  if (path.startsWith('/')) {
    let sitePath = path
    if (BASE_NO_SLASH && sitePath.startsWith(BASE_NO_SLASH)) {
      sitePath = sitePath.slice(BASE_NO_SLASH.length) || '/'
    }
    if (isImage) {
      const publicFile = join(PROJECT_ROOT, projectConfig.siteDir, '.vitepress', 'public', sitePath)
      if (existsSync(publicFile)) return
      const inVolume = volumes.map(v => join(DOCS, v.srcDir, sitePath.replace(/^\//, '')))
      if (inVolume.some(p => existsSync(p))) return
      failures.push({ file, line, target, reason: '图片不存在(public 与各卷源目录都没有)' })
      return
    }
    if (!urlToFile(sitePath)) {
      failures.push({ file, line, target, reason: '站内路径反查不到源文件' })
    }
    return
  }

  // 相对路径
  if (isImage) {
    const abs = resolve(dirname(file), path)
    if (existsSync(abs)) return
    failures.push({ file, line, target, reason: '图片不存在' })
    return
  }

  // 日志卷的站外相对链接(源码、todo、ai)在渲染层被 source-link-plugin
  // 改写为 GitHub URL,源文件层放行,与渲染行为对齐:解析落点不在上站
  // 目录、或在上站目录但文件不存在(裸相对路径按仓库根坐标写,渲染层
  // 有仓库根回退)都算会被改写。
  const inNotes = relative(DOCS, file).startsWith('notes/')
  const abs = resolve(dirname(file), path)
  const withinDocs = relative(DOCS, abs)
  if (inNotes && withinDocs.startsWith('..')) return
  if (inNotes) {
    const firstSeg = withinDocs.split('/')[0] ?? ''
    if (!PUBLISHED_VOLUME_DIRS.includes(firstSeg)) return
    if (!existsSync(`${abs}.md`) && !existsSync(abs)) return
  }
  if (withinDocs.startsWith('..')) {
    failures.push({ file, line, target, reason: '指向 document/ 之外(站内没有该目标)' })
    return
  }
  if (existsSync(abs) && statSync(abs).isFile()) return

  const withMd = `${abs}.md`
  if (existsSync(withMd)) return

  const asIndex = join(abs, 'index.md')
  if (existsSync(asIndex)) return

  failures.push({ file, line, target, reason: '目标文件不存在' })
}

function checkFile(volume: Volume, file: string): void {
  checkedFiles++
  const lines = readFileSync(file, 'utf-8').split('\n')
  lines.forEach((text, idx) => {
    const lineNo = idx + 1
    // markdown 行内链接与图片
    const linkRe = /(!?)\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)/g
    let m: RegExpExecArray | null
    while ((m = linkRe.exec(text)) !== null) {
      checkTarget(file, lineNo, m[2], m[1] === '!')
    }
    // HTML 链接
    const hrefRe = /<a\s[^>]*href="([^"]+)"/gi
    while ((m = hrefRe.exec(text)) !== null) {
      checkTarget(file, lineNo, m[1], false)
    }
  })
}

for (const volume of volumes) {
  const dir = join(DOCS, volume.srcDir)
  if (!existsSync(dir)) {
    // 生成卷(api)在未跑 docs:api 时不存在:跳过但不失败,CI 顺序保证
    // 链接检查跑在 api 生成之后;真缺目录时这里只提示
    console.warn(`warn: 卷目录 ${volume.srcDir} 不存在(生成卷未生成?),跳过该卷`)
    continue
  }
  for (const file of walk(dir)) {
    checkFile(volume, file)
  }
}

console.log(`检查 ${checkedFiles} 个文件、${checkedLinks} 条链接`)

if (failures.length > 0) {
  console.error(`\n发现 ${failures.length} 条站内链接问题:\n`)
  for (const f of failures) {
    const rel = relative(PROJECT_ROOT, f.file)
    console.error(`  ${rel}:${f.line}  ${f.target}  —  ${f.reason}`)
  }
  process.exit(1)
}

console.log('站内链接全部可达')
