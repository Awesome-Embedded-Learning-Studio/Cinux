import { nextTick, onMounted } from 'vue'
import { subscribeAfterRouteChange } from './router-hooks'

/**
 * API 参考页的客户端装饰。
 *
 * 生成器产出的 HTML 是平的：函数名是一整串文本、方向是裸的 in/out、
 * 源码行是「源码 path:42」一段字。CSS 读不到文本内容，所以这几处语义
 * 由这里补：拆命名空间、标方向、把行号拆成可点的章。
 *
 * 只在 API 页面跑，且带幂等标记——路由往返、水合重渲都只装饰一次。
 * 没有 JS 时页面照常可读，只是少了这些润色。
 */

const API_SCOPE = '.vp-doc[class*="_Cinux_api_"]'
const LABELS = new Set(['返回', '被调用', '注', '警告'])
const DIRECTIONS = new Set(['in', 'out', 'inout'])
const MARK = 'apiDecorated'

function decorate(root: HTMLElement): void {
  if (root.dataset[MARK] === '1') return
  root.dataset[MARK] = '1'

  // ── 函数名：拆成「淡色命名空间 + 粗体函数名」 ──────────────
  root.querySelectorAll<HTMLElement>('h4').forEach((h4) => {
    if (h4.querySelector('.api-fn')) return
    const textNode = Array.from(h4.childNodes).find(
      (n) => n.nodeType === 3 && (n.textContent || '').trim() !== '',
    )
    if (!textNode) return
    const full = textNode.textContent || ''
    const cut = full.lastIndexOf('::')
    if (cut < 0) return

    const ns = document.createElement('span')
    ns.className = 'api-ns'
    ns.textContent = full.slice(0, cut + 2)

    const fn = document.createElement('span')
    fn.className = 'api-fn'
    fn.textContent = full.slice(cut + 2)

    textNode.replaceWith(ns, fn)
  })

  // ── 参数表方向列：标成 data-dir，配色和图标由 CSS 接 ──────
  root.querySelectorAll<HTMLTableElement>('figure.doc-table table').forEach((table) => {
    const heads = Array.from(table.querySelectorAll('thead th'))
    const col = heads.findIndex((th) => (th.textContent || '').trim() === '方向')
    if (col < 0) return

    table.querySelectorAll('tbody tr').forEach((tr) => {
      const cell = tr.children[col] as HTMLElement | undefined
      const code = cell?.querySelector('code')
      const dir = (code?.textContent || '').trim().toLowerCase()
      if (cell && code && DIRECTIONS.has(dir)) cell.dataset.dir = dir
    })
  })

  // ── 结构标签：返回 / 被调用 / 注 ──────────────────────────
  root.querySelectorAll('p > strong:first-child').forEach((strong) => {
    const label = (strong.textContent || '').trim()
    const p = strong.parentElement
    if (p && LABELS.has(label)) p.dataset.label = label
  })

  // ── 元数据行：自 x.x.x · 源码 path:42 ────────────────────
  root.querySelectorAll<HTMLElement>('p').forEach((p) => {
    const link = p.querySelector<HTMLAnchorElement>('a[href*="github.com"]')
    if (!link || !/源码/.test(link.textContent || '')) return
    p.dataset.meta = '1'
    if (link.querySelector('.api-line')) return

    // 行号单独包成章：把结尾的 :42 拆出来
    const text = link.textContent || ''
    const m = text.match(/^(.*?)(:(\d+))\s*$/)
    if (!m) return
    link.textContent = ''
    link.append(m[1], ':')
    const chip = document.createElement('span')
    chip.className = 'api-line'
    chip.textContent = m[3]
    link.append(chip)
  })
}

function scan(): void {
  if (typeof document === 'undefined') return
  const root = document.querySelector<HTMLElement>(API_SCOPE)
  if (root) decorate(root)
}

export function setupApiDecor(): void {
  onMounted(() => {
    void nextTick(() => scan())
  })
  subscribeAfterRouteChange(() => {
    void nextTick(() => scan())
  })
}
