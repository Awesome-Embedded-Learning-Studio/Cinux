// 符号转换:compound XML → ApiSymbol[]。
//
// doxygen 的 GROUPD_MEMBER 机制:被 @ingroup 的符号,完整 memberdef 定义
// 挪进 group compound,原容器(namespace/class/struct)只留 refid 引用。
// 因此符号收集分两路,合起来恰是全集、refid 无碰撞:
//   1. group compound 的 memberdef → @ingroup 符号(组归属天然同源)
//   2. namespace/class/struct 的 memberdef → 未分组符号
// file compound(宏、文件级自由函数)不收:同一符号在 file 里另有副本。

import { readFileSync, readdirSync } from 'node:fs'
import { join } from 'node:path'
import { createXmlParser, asArray, textOf, type XmlNode, type DescNode } from '../parse/xml'
import type { DoxyCompound, DoxyCompoundDef, DoxyMemberDef } from '../parse/doxygen'
import { extractBrief, extractDetails, extractDetailProse, flattenPlain } from './doc'
import type { ApiSymbol, ApiParam, ApiEnumValue } from '../model/symbol'

const CONTAINER_KINDS = new Set(['namespace', 'class', 'struct', 'union', 'interface'])

/** location.file 的仓库前缀裁剪:doxygen 输出绝对路径时去掉仓库根 */
export function makeRelative(file: string, projectRoot: string): string {
  const norm = file.replace(/\\/g, '/')
  if (norm.startsWith(projectRoot)) return norm.slice(projectRoot.length).replace(/^\//, '')
  return norm
}

/** 解析一个 compound XML 文件 */
export function parseCompound(file: string, projectRoot: string): DoxyCompound | null {
  const parser = createXmlParser()
  const parsed = parser.parse(readFileSync(file, 'utf-8')) as DoxyCompound
  const def = parsed?.doxygen?.compounddef
  if (!def) return null
  if (def.location?.['@file']) {
    def.location['@file'] = makeRelative(def.location['@file'], projectRoot)
  }
  return parsed
}

/** 单个 memberdef → ApiSymbol;groupName 非 undefined 时回填组归属 */
export function memberToSymbol(
  member: DoxyMemberDef,
  container: DoxyCompoundDef,
  projectRoot: string,
  groupName?: string,
): ApiSymbol | null {
  const name = textOf(member.name)
  if (!name) return null

  const kind = member['@kind'] ?? 'unknown'
  const qualified = textOf(member.qualifiedname) || name
  const definition = textOf(member.definition)
  const args = textOf(member.argsstring)
  const signature = definition ? definition + args : `${flattenPlain(member.type as DescNode)} ${name}${args}`

  const detail = extractDetails(member.detaileddescription as DescNode)
  const docParams = new Map(detail.params.map(p => [p.name, p]))
  const params: ApiParam[] = asArray<XmlNode>(member.param).map((p: XmlNode) => {
    const pname = textOf(p.declname) || textOf(p.defname)
    const ptype = flattenPlain(p.type as DescNode)
    const doc = docParams.get(pname)
    return {
      name: pname,
      type: ptype,
      direction: doc?.direction,
      description: doc?.description ?? '',
    }
  })

  const brief = extractBrief(member.briefdescription)
  // 枚举成员的说明各在自己的 briefdescription 里,doxygen 把它们挂在
  // memberdef 的 enumvalue 节点下,转换器此前只读签名,页面因此只剩一行。
  const values: ApiEnumValue[] = asArray<XmlNode>(member.enumvalue).map((v: XmlNode) => ({
    name: textOf(v.name),
    value: textOf(v.initializer),
    brief: extractBrief(v.briefdescription as DescNode),
  })).filter(v => v.name)
  // 声明处优先(API 文档读者要的是头文件里的签名);无声明属性才用定义处
  const declFile = member.location?.['@declfile']
  const loc = declFile
    ? {
        file: makeRelative(declFile, projectRoot),
        line: parseInt(member.location?.['@declline'] ?? '0', 10),
      }
    : member.location?.['@file']
      ? {
          file: makeRelative(member.location['@file'], projectRoot),
          line: parseInt(member.location?.['@line'] ?? '0', 10),
        }
      : { file: '', line: 0 }

  return {
    id: member['@id'],
    kind,
    name,
    qualifiedName: qualified,
    parent: container.compoundname ? textOf(container.compoundname) : '',
    parentKind: container['@kind'],
    signature,
    returnType: flattenPlain(member.type as DescNode),
    params,
    brief,
    details: extractDetailProse(member.detaileddescription),
    returns: detail.returns,
    note: detail.note,
    warning: detail.warning,
    since: detail.since,
    groups: groupName ? [groupName] : [],
    referencedBy: asArray<XmlNode>(member.referencedby).map((r) => ({
      id: typeof r['@refid'] === 'string' ? r['@refid'] : '',
      name: textOf(r['#text'] ?? r),
    })).filter(r => r.id && r.name),
    location: loc,
    values,
    documented: brief !== '',
  }
}

function membersOf(def: DoxyCompoundDef): DoxyMemberDef[] {
  const out: DoxyMemberDef[] = []
  for (const section of asArray<XmlNode>(def.sectiondef)) {
    for (const member of asArray<DoxyMemberDef>((section as XmlNode).memberdef as DoxyMemberDef[] | DoxyMemberDef)) {
      out.push(member)
    }
  }
  return out
}

export interface CollectResult {
  symbols: ApiSymbol[]
}

/** 扫 xml 目录:group + container 两路符号,按 id 去重、稳定排序 */
export function collectSymbols(xmlDir: string, projectRoot: string): CollectResult {
  const byId = new Map<string, ApiSymbol>()

  for (const entry of readdirSync(xmlDir)) {
    if (!entry.endsWith('.xml')) continue
    if (entry === 'index.xml' || entry === 'Doxyfile.xml') continue
    const compound = parseCompound(join(xmlDir, entry), projectRoot)
    const def = compound?.doxygen?.compounddef
    if (!def) continue

    if (def['@kind'] === 'group') {
      const gname = textOf(def.compoundname)
      for (const member of membersOf(def)) {
        const sym = memberToSymbol(member, def, projectRoot, gname)
        if (sym) byId.set(sym.id, sym)
      }
    } else if (CONTAINER_KINDS.has(def['@kind'])) {
      for (const member of membersOf(def)) {
        const sym = memberToSymbol(member, def, projectRoot)
        if (sym) byId.set(sym.id, sym)
      }
    }
  }

  const symbols = Array.from(byId.values()).sort((a, b) =>
    a.qualifiedName === b.qualifiedName
      ? a.id.localeCompare(b.id)
      : a.qualifiedName.localeCompare(b.qualifiedName),
  )
  return { symbols }
}
