// emit 层:ApiBundle → 磁盘 JSON 文件。
//
// 稳定性是硬要求:同一份 XML 两次转换,产物必须逐字节一致(CI 的 diff
// 才干净)。做法是所有集合在 transform 层已排序,这里只做确定性的
// JSON.stringify;generatedAt 是"数据快照时间",单独放 meta,需要
// 逐字节复现的校验流程比对时忽略该字段。

import { mkdirSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import type { ApiBundle } from '../model/symbol'

export function serializeBundle(bundle: ApiBundle): string {
  return JSON.stringify(bundle, null, 2) + '\n'
}

export function emitBundle(bundle: ApiBundle, outDir: string): string[] {
  mkdirSync(outDir, { recursive: true })
  // 单文件 bundle + 按需的拆分文件:M0 出单文件,消费端一次 fetch
  const file = join(outDir, 'api-bundle.json')
  writeFileSync(file, serializeBundle(bundle))
  return [file]
}
