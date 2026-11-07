// 注释清洗:doxygen XML 的描述体是嵌套 markup(para/ref/simplesect/
// parameterlist…),这里挤成纯文本。
//
// 走访原则:一个对象的所有子键都要看。doxygen 的 para 常把参数表、
// 返回段、注记段混排在同一个 para 里,遇到一种结构就停会丢其余段落。

import { asArray, textOf, type XmlNode } from '../parse/xml'

/** 描述节点的归一形状(由 fast-xml-parser 产出) */
type DescNode = string | XmlNode | Array<string | XmlNode> | undefined

function isNone(text: string): boolean {
  return /^(none|无)$/i.test(text.trim())
}

/** 同 flattenDesc,但 ref/computeroutput 原样输出(类型文本用,不带反引号) */
export function flattenPlain(node: DescNode): string {
  const coded = flattenDesc(node)
  return coded.replace(/`([^`]*)`/g, '$1')
}

/** 把任意描述节点挤成纯文本;ref/computeroutput 输出为 `code` */
export function flattenDesc(node: DescNode): string {  if (node === undefined || node === null) return ''
  if (typeof node === 'string') return node.trim()
  if (typeof node === 'number') return String(node)
  if (Array.isArray(node)) {
    return node.map(flattenDesc).filter(Boolean).join(' ')
  }
  const obj = node as XmlNode
  const parts: string[] = []
  for (const key of Object.keys(obj)) {
    if (key === '#text') {
      const t = textOf(obj['#text'])
      if (t) parts.push(t)
    } else if (key === 'ref' || key === 'computeroutput') {
      const inner = flattenDesc(obj[key])
      if (inner) parts.push(`\`${inner}\``)
    } else if (key === 'para') {
      // 段落之间换行
      const paras = asArray(obj.para as DescNode)
      const joined = paras.map(flattenDesc).filter(Boolean).join('\n')
      if (joined) parts.push(joined)
    } else if (key === 'parameterlist' || key === 'simplesect') {
      // 结构化段不做纯文本展开(由 extractDetails 单独处理)
      continue
    } else if (!key.startsWith('@')) {
      const t = flattenDesc(obj[key] as DescNode)
      if (t) parts.push(t)
    }
  }
  return parts.join(' ')
}

/** briefdescription 节点 → 纯文本(丢弃占位值) */
export function extractBrief(brief: DescNode): string {
  const text = flattenDesc(brief)
  return isNone(text) ? '' : text
}

/** detaileddescription 结构化字段 */
export interface DetailFields {
  params: { name: string, direction?: 'in' | 'out' | 'inout', description: string }[]
  returns: string
  note: string
  warning: string
  since: string
  /** 模板参数(typedef/类场景),M0 先收文本 */
  tparams: { name: string, description: string }[]
}

/** @since 写法归一:v0.3.0 / 0.3.0 → 0.3.0 */
export function normalizeSince(raw: string): string {
  return raw.trim().replace(/^v(?=\d)/i, '')
}

/** 从 detaileddescription 抽结构化段落:遍历全部节点,不提前停 */
export function extractDetails(detail: DescNode): DetailFields {
  const out: DetailFields = {
    params: [], returns: '', note: '', warning: '', since: '', tparams: [],
  }

  const walk = (node: DescNode): void => {
    if (node === undefined || node === null) return
    if (typeof node === 'string') return
    if (Array.isArray(node)) {
      node.forEach(walk)
      return
    }
    const obj = node as XmlNode

    for (const key of Object.keys(obj)) {
      if (key.startsWith('@') || key === '#text') continue

      if (key === 'parameterlist') {
        for (const list of asArray(obj.parameterlist as XmlNode)) {
          const kind = textOf((list as XmlNode)['@kind'])
          for (const item of asArray((list as XmlNode).parameteritem as XmlNode)) {
            const nameLists = asArray((item as XmlNode).parameternamelist as XmlNode)
            for (const nl of nameLists) {
              for (const pn of asArray((nl as XmlNode).parametername as XmlNode)) {
                const pname = textOf((pn as XmlNode)['#text'] ?? pn)
                const dir = textOf((pn as XmlNode)['@direction']) as 'in' | 'out' | 'inout' | ''
                const desc = flattenDesc((item as XmlNode).parameterdescription)
                if (!pname || isNone(pname)) continue
                if (kind === 'param') {
                  out.params.push({
                    name: pname,
                    direction: dir || undefined,
                    description: desc,
                  })
                } else if (kind === 'templateparam') {
                  out.tparams.push({ name: pname, description: desc })
                }
                // exception:M0 不收
              }
            }
          }
        }
        continue
      }

      if (key === 'simplesect') {
        for (const sect of asArray(obj.simplesect as XmlNode)) {
          const kind = textOf((sect as XmlNode)['@kind'])
          const text = flattenDesc((sect as XmlNode).para)
          if (isNone(text)) continue
          if (kind === 'return') out.returns ||= text
          else if (kind === 'note') out.note ||= text
          else if (kind === 'warning') out.warning ||= text
          else if (kind === 'since') {
            out.since ||= normalizeSince(text)
          }
        }
        continue
      }

      // 其余容器键(para 等):递归
      walk(obj[key] as DescNode)
    }
  }

  walk(detail)
  return out
}

/** 详情主文:剥掉参数表/返回/注记段后剩下的正文段落 */
export function extractDetailProse(detail: DescNode): string {
  const lines: string[] = []
  const walk = (node: DescNode): void => {
    if (node === undefined || node === null) return
    if (typeof node === 'string') return
    if (Array.isArray(node)) {
      node.forEach(walk)
      return
    }
    const obj = node as XmlNode
    for (const key of Object.keys(obj)) {
      if (key.startsWith('@') || key === '#text') continue
      if (key === 'para') {
        for (const p of asArray(obj.para as DescNode)) {
          if (typeof p === 'string') {
            const t = p.trim()
            if (t) lines.push(t)
          } else {
            // 只收"无结构化子键"的段落为正文
            const keys = Object.keys(p as XmlNode).filter(
              k => !k.startsWith('@') && k !== '#text'
              && k !== 'ref' && k !== 'computeroutput' && k !== 'ulink' && k !== 'bold' && k !== 'emphasis' && k !== 'linebreak'
            )
            if (keys.length === 0) {
              const t = flattenDesc(p)
              if (t) lines.push(t)
            }
          }
        }
        continue
      }
      walk(obj[key] as DescNode)
    }
  }
  walk(detail)
  return lines.join('\n')
}
