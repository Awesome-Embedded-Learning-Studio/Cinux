// 类型正本:转换器的输出契约。
//
// 双重身份:emit 层按它产 JSON,VitePress 侧(M1 起)的组件 import 它读
// JSON。字段变更就是接口变更,两边一起动。

/** 函数参数 */
export interface ApiParam {
  name: string
  type: string
  /** @param[in]/[out]/[inout] 的方向标记,无标记为 undefined */
  direction?: 'in' | 'out' | 'inout'
  /** 参数说明(纯文本) */
  description: string
}

/** 枚举成员:名字、可选的初始化值、说明 */
export interface ApiEnumValue {
  name: string
  /** 源码里的初始化表达式,例如 0x04,未写时为空串 */
  value: string
  brief: string
}

/** 单个符号(函数/变量/类型成员) */
export interface ApiSymbol {
  /** doxygen refid,如 namespacecinux_1_1console_1a9475…,站内唯一 */
  id: string
  kind: string
  name: string
  qualifiedName: string
  /** 所属容器(namespace/class/struct)的限定名,顶层符号为空串 */
  parent: string
  parentKind: string
  /** definition + argsstring,可直接渲染的签名 */
  signature: string
  /** 返回类型原文 */
  returnType: string
  params: ApiParam[]
  /** @brief,纯文本 */
  brief: string
  /** 详情段中 note 之外的正文段落,纯文本 */
  details: string
  /** @return,纯文本 */
  returns: string
  /** @note */
  note: string
  /** @warning */
  warning: string
  /** @since,原文(如 "0.1.0"),未标注为空串 */
  since: string
  /** 所属 @ingroup 组名列表 */
  groups: string[]
  /** 被谁调用:静态分析到的引用方,refid + 显示名(doxygen referencedby) */
  referencedBy: { id: string, name: string }[]
  /** 源码位置,file 是仓库相对路径(保证跨机构建产物一致) */
  location: { file: string, line: number }
  /** 枚举的成员列表,只有 kind 为 enum 时非空 */
  values: ApiEnumValue[]
  /** 文档完备度标记,coverage 统计与展示分层用 */
  documented: boolean
}

/** @ingroup 分组 */
export interface ApiGroup {
  name: string
  title: string
  /** 组内符号的 refid,按符号 qualifiedName 排序 */
  symbolIds: string[]
}

/** 覆盖率统计的一行(按组) */
export interface CoverageEntry {
  group: string
  title: string
  /** 组内符号总数 */
  total: number
  /** 有 @brief 的符号数 */
  withBrief: number
  /** 有 @since 的符号数 */
  withSince: number
  /** 函数里参数说明齐全的比例(非函数符号不计入分母) */
  paramDocs: { covered: number, total: number } | null
}

/** 搜索索引条目(M1 的搜索面板吃这个) */
export interface SearchEntry {
  id: string
  name: string
  qualifiedName: string
  brief: string
  kind: string
  group: string
}

/** emit 层的完整产物 */
export interface ApiBundle {
  symbols: ApiSymbol[]
  groups: ApiGroup[]
  coverage: CoverageEntry[]
  searchIndex: SearchEntry[]
  /** 管线元信息:数据来源与生成时间 */
  meta: {
    generatedAt: string
    symbolCount: number
    groupCount: number
  }
}
