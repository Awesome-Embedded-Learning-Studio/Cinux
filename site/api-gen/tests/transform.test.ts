// 金样本 + 边角用例。
//
// fixture 是三份小 XML:console namespace(真实裁剪,变量+函数引用)、
// kernel_boot group(真实裁剪,四个函数)、edge namespace(手工造的重载/
// v 前缀 since/无注释/占位 brief)。

import { describe, it, expect } from 'vitest'
import { resolve } from 'node:path'
import { runPipeline } from '../src/index'

const ROOT = resolve(import.meta.dirname, '..')               // site/api-gen
const PROJECT_ROOT = resolve(ROOT, '../..')                  // 仓库根
const FIXTURES = resolve(ROOT, 'tests/fixtures/xml')

function bundle() {
  return runPipeline({ xmlDir: FIXTURES, projectRoot: PROJECT_ROOT })
}

describe('金样本:console + kernel_boot', () => {
  it('InitConsole 从 group compound 收符号,组归属回填', () => {
    const b = bundle()
    const init = b.symbols.find(
      s => s.name === 'InitConsole' && s.groups.includes('kernel_boot'),
    )
    expect(init).toBeDefined()
    expect(init!.since).toBe('0.1.0')
    expect(init!.brief).toContain('kernel console up')
    expect(init!.signature).toBe('void cinux::console::InitConsole(const cinux::boot::BootInfo &info)')
    expect(init!.params).toHaveLength(1)
    expect(init!.params[0].name).toBe('info')
    expect(init!.params[0].direction).toBe('in')
    expect(init!.params[0].type).toContain('BootInfo')
    expect(init!.returns).toBe('')  // @return None 是占位值,过滤为空
    expect(init!.location.file).toBe('kernel/console/console.hpp')
    expect(init!.location.line).toBeGreaterThan(0)
  })

  it('kernel_boot 组存在且包含四个函数', () => {
    const b = bundle()
    const g = b.groups.find(x => x.name === 'kernel_boot')
    expect(g).toBeDefined()
    expect(g!.title).toContain('Kernel Boot')
    const names = g!.symbolIds
      .map(id => b.symbols.find(s => s.id === id)?.name)
      .filter(Boolean)
    for (const fn of ['InitConsole', 'PutChar', 'PutString', 'Flush']) {
      expect(names).toContain(fn)
    }
  })

  it('变量符号也从容器收进来(kCharNewline)', () => {
    const b = bundle()
    const v = b.symbols.find(s => s.name === 'kCharNewline')
    expect(v).toBeDefined()
    expect(v!.kind).toBe('variable')
    expect(v!.qualifiedName).toBe('cinux::console::kCharNewline')
  })
})

describe('枚举取值', () => {
  it('取值名、初始化值与说明都从 enumvalue 节点取出来', () => {
    const b = bundle()
    const mode = b.symbols.find(s => s.name === 'Mode' && s.kind === 'enum')
    expect(mode).toBeDefined()
    expect(mode!.values).toHaveLength(2)
    expect(mode!.values[0].name).toBe('kIdle')
    expect(mode!.values[0].value).toBe('= 0')
    expect(mode!.values[0].brief).toContain('Nothing in flight')
    expect(mode!.values[1].name).toBe('kBusy')
    expect(mode!.values[1].value).toBe('= 2')
  })

  it('非枚举符号的 values 为空数组', () => {
    const b = bundle()
    const fn = b.symbols.find(s => s.name === 'Measure')
    expect(fn!.values).toEqual([])
  })
})

describe('边角:重载 / since 写法 / 占位值', () => {
  it('同名重载各自成页,id 不同、不互相覆盖', () => {
    const b = bundle()
    const measures = b.symbols.filter(s => s.qualifiedName === 'cinux::edge::Measure')
    expect(measures).toHaveLength(2)
    expect(new Set(measures.map(m => m.id)).size).toBe(2)
    const withDefault = measures.find(m => m.params[0]?.name === 'count')
    expect(withDefault?.params[0]?.type).toBe('size_t')
    // 搜索索引两条都在
    const idx = b.searchIndex.filter(e => e.qualifiedName === 'cinux::edge::Measure')
    expect(idx).toHaveLength(2)
  })

  it('since 的 v 前缀归一为裸版本号', () => {
    const b = bundle()
    const v = b.symbols.find(s => s.qualifiedName === 'cinux::edge::Measure' && s.params[0]?.name === 'count')
    expect(v!.since).toBe('0.2.0')   // fixture 里写的是 v0.2.0
    const plain = b.symbols.find(s => s.qualifiedName === 'cinux::edge::Measure' && s.params[0]?.name === 'label')
    expect(plain!.since).toBe('0.3.0')
  })

  it('无 brief 与占位值 brief 都算未文档化', () => {
    const b = bundle()
    const un = b.symbols.find(s => s.name === 'Undocumented')
    expect(un!.documented).toBe(false)
    expect(un!.brief).toBe('')
    const ph = b.symbols.find(s => s.name === 'Placeholder')
    expect(ph!.documented).toBe(false)
    // 占位 warning "None" 不进 warning 字段
    expect(un!.warning).toBe('')
  })
})

describe('调用关系(referencedby)', () => {
  it('命中符号表的引用方显示限定名,未命中的保留原始名', () => {
    const b = bundle()
    const m = b.symbols.find(s => s.qualifiedName === 'cinux::edge::Measure' && s.params[0]?.name === 'count')
    expect(m).toBeDefined()
    expect(m!.referencedBy).toHaveLength(2)
    const names = m!.referencedBy.map(r => r.name)
    // refid 指向同 fixture 的重载二 → 限定名
    expect(names).toContain('cinux::edge::Measure')
    // refid 不在符号表 → 原始名
    expect(names).toContain('ExternalCaller')
  })
})

describe('稳定性', () => {
  it('同一输入两次转换,除 generatedAt 外逐字节一致', () => {
    const strip = (o: unknown): string =>
      JSON.stringify(o, (k, v) => (k === 'generatedAt' ? '<TS>' : v))
    const a = bundle()
    const c = bundle()
    expect(strip(a)).toBe(strip(c))
  })

  it('符号与组列表排序确定', () => {
    const b = bundle()
    const names = b.symbols.map(s => s.qualifiedName)
    const sorted = [...names].sort((x, y) => x.localeCompare(y))
    expect(names).toEqual(sorted)
    const gnames = b.groups.map(g => g.name)
    expect(gnames).toEqual([...gnames].sort((x, y) => x.localeCompare(y)))
  })
})
