// cli 入口:doxygen XML → api-bundle.json
//
// 用法:tsx site/api-gen/src/cli.ts [--xml <dir>] [--out <dir>]
//   --xml  doxygen XML 输出目录,默认 site/api-gen/xml
//   --out  产物目录,默认 site/api-gen/dist

import { resolve } from 'node:path'
import { runPipeline } from './index'
import { emitBundle } from './emit/json'
import { emitPages } from './emit/pages'

function argValue(flag: string, fallback: string): string {
  const i = process.argv.indexOf(flag)
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback
}

const projectRoot = resolve(import.meta.dirname, '../../..')
const xmlDir = resolve(projectRoot, argValue('--xml', 'site/api-gen/xml'))
const outDir = resolve(projectRoot, argValue('--out', 'site/api-gen/dist'))

const t0 = Date.now()
const bundle = runPipeline({ xmlDir, projectRoot })
const files = emitBundle(bundle, outDir)
// 页面骨架:生成到 document/api/,由 VitePress 的 api 卷消费
emitPages(bundle, {
  outDir: resolve(projectRoot, 'document/api'),
  repo: {
    owner: 'Awesome-Embedded-Learning-Studio',
    repo: 'Cinux',
    branch: 'main',
  },
})

console.log(`api-gen: ${bundle.meta.symbolCount} 个符号、${bundle.meta.groupCount} 个组 → ${files.join(', ')} (${((Date.now() - t0) / 1000).toFixed(1)}s)`)
