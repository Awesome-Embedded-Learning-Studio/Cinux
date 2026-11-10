// doxygen compound XML 的类型声明。
//
// schema 本身见 doxygen 自带的 compound.xsd,十年稳定;这里只声明转换器
// 实际取用的字段,声明从紧,取值从宽(经 asArray/textOf 归一)。

import type { XmlNode, DescNode } from './xml'

/** compounddef 的 kind 枚举(转换器关心的子集) */
export type CompoundKind =
  | 'namespace'
  | 'class'
  | 'struct'
  | 'union'
  | 'interface'
  | 'group'
  | 'file'

export interface DoxyLocation {
  '@file'?: string
  '@line'?: string
  '@column'?: string
  '@declfile'?: string
  '@declline'?: string
  '@bodystart'?: string
  '@bodyend'?: string
}

/** parameterlist 的一个参数项 */
export interface DoxyParamItem {
  parameternamelist?: unknown
  parameterdescription?: unknown
}

/** memberdef:函数/变量/枚举等单个符号 */
export interface DoxyMemberDef {
  '@id': string
  '@kind': string
  '@prot'?: string
  '@static'?: string
  '@virt'?: string
  type?: unknown
  definition?: unknown
  argsstring?: unknown
  name?: unknown
  qualifiedname?: unknown
  initializer?: unknown
  param?: XmlNode | XmlNode[]
  enumvalue?: XmlNode | XmlNode[]
  briefdescription?: DescNode
  detaileddescription?: DescNode
  inbodydescription?: DescNode
  location?: DoxyLocation
  references?: unknown
  referencedby?: XmlNode | XmlNode[]
}

/** compounddef:namespace/class/struct/group 容器 */
export interface DoxyCompoundDef {
  '@id': string
  '@kind': CompoundKind
  '@language'?: string
  compoundname?: unknown
  title?: unknown
  briefdescription?: DescNode
  detaileddescription?: DescNode
  sectiondef?: XmlNode | XmlNode[]
  innerclass?: unknown
  innernamespace?: unknown
  innergroup?: unknown
  location?: DoxyLocation
}

/** 一个 compound XML 文件的根 */
export interface DoxyCompound {
  '?xml'?: unknown
  doxygen: {
    compounddef: DoxyCompoundDef
  }
}
