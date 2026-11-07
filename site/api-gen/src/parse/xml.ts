// fast-xml-parser 封装:解析选项集中一处,防 prototype 污染。

import { XMLParser } from 'fast-xml-parser'

export interface ParseOptions {
  /** doxygen XML 里 para 等元素的文本节点属性名 */
  textNodeName: string
  ignoreAttributes: false
  parseTagValue: boolean
  trimValues: boolean
}

export function createXmlParser(): XMLParser {
  return new XMLParser({
    // doxygen 的属性(refid/kind/direction/file/line…)全都要
    ignoreAttributes: false,
    attributeNamePrefix: '@',
    // 文本保持原样:行号等数字字段由取用方自行转换
    parseTagValue: false,
    parseAttributeValue: false,
    trimValues: false,
    textNodeName: '#text',
    // 不解析实体为对象,保持字符串;&amp; 等由 parser 内部解码
    processEntities: true,
  })
}

export type XmlNode = Record<string, unknown>

/** 数组/单值/undefined 归一成数组 */
export function asArray<T>(v: T | T[] | undefined | null): T[] {
  if (v === undefined || v === null) return []
  return Array.isArray(v) ? v : [v]
}

/** 取文本:fast-xml-parser 里同一字段可能是 string / object{#text} / 数组 */
export function textOf(v: unknown): string {
  if (v === undefined || v === null) return ''
  if (typeof v === 'string') return v.trim()
  if (typeof v === 'number') return String(v)
  if (Array.isArray(v)) return v.map(textOf).filter(Boolean).join(' ')
  if (typeof v === 'object' && '#text' in (v as XmlNode)) {
    return textOf((v as XmlNode)['#text'])
  }
  return ''
}
