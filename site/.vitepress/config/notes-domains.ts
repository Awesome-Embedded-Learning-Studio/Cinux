// 开发日志的域映射与标题清洗。
//
// 日志文件名形如 2026-06-16-f1-m2-kernel-logging.md 或
// 2026-06-27-mm-cow-relax-u-bit-gate.md;域编号对应 todo/ 下的
// f<N>-<主题> 目录,finfra 是 f1(kernel-infra)的连写变体。
// 无域编号的主题词笔记统一归"专题"组。
// sidebar(运行时)与 gen-notes-index(构建时)共用这一份映射。

export interface NoteDomain {
  id: string
  /** 文件名中匹配域的表达式 */
  match: RegExp
  title: string
}

export const NOTE_DOMAINS: NoteDomain[] = [
  { id: 'f1', match: /^2026-\d\d-\d\d-f1-|^2026-\d\d-\d\d-finfra-/, title: 'F1 内核基建' },
  { id: 'f2', match: /^2026-\d\d-\d\d-f2-/, title: 'F2 内存管理' },
  { id: 'f3', match: /^2026-\d\d-\d\d-f3-/, title: 'F3 进程' },
  { id: 'f4', match: /^2026-\d\d-\d\d-f4-/, title: 'F4 SMP' },
  { id: 'f5', match: /^2026-\d\d-\d\d-f5-/, title: 'F5 驱动' },
  { id: 'f6', match: /^2026-\d\d-\d\d-f6-/, title: 'F6 VFS' },
  { id: 'f7', match: /^2026-\d\d-\d\d-f7-/, title: 'F7 网络' },
  { id: 'f8', match: /^2026-\d\d-\d\d-f8-/, title: 'F8 IPC' },
  { id: 'f9', match: /^2026-\d\d-\d\d-f9-/, title: 'F9 安全' },
  { id: 'f10', match: /^2026-\d\d-\d\d-f10-/, title: 'F10 用户态' },
  { id: 'f11', match: /^2026-\d\d-\d\d-f11-/, title: 'F11 平台' },
  { id: 'f12', match: /^2026-\d\d-\d\d-f12-/, title: 'F12 开发者设施' },
  { id: 'f13', match: /^2026-\d\d-\d\d-f13-/, title: 'F13 GUI' },
]

export const TOPIC_GROUP_TITLE = '专题与后续迭代'

export function domainOf(fileName: string): NoteDomain | null {
  for (const d of NOTE_DOMAINS) {
    if (d.match.test(fileName)) return d
  }
  return null
}

/** H1/文件名 → 侧栏短标题:去掉日期与域标记 */
export function noteTitle(fileName: string, h1: string | null): string {
  if (h1) {
    const cleaned = h1
      .replace(/^#\s*/, '')
      .replace(/^20\d\d-\d\d-\d\d\s*/, '')
      .replace(/^F[\dA-Za-z]+[-\w]*\s*/, '')
      .trim()
    if (cleaned) return cleaned
  }
  return fileName
    .replace(/^20\d\d-\d\d-\d\d-/, '')
    .replace(/\.md$/, '')
    .replace(/[-_]/g, ' ')
}
