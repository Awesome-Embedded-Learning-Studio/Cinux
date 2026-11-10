import { describe, expect, it } from 'vitest'
import { evaluateAlignment } from '../../.vitepress/theme/math-playground'

describe('对齐实验室', () => {
  it.each([
    ['4097', '4096', 'down', 4096],
    ['4097', '4096', 'up', 8192],
    ['8192', '4096', 'down', 8192],
    ['8192', '4096', 'up', 8192],
    ['0', '4096', 'up', 0],
    ['10', '3', 'down', 9],
    ['10', '3', 'up', 12],
    ['1000000000', '65536', 'up', 1000013824],
  ] as const)('%s 按 %s 向 %s 对齐为 %s', (value, unit, direction, expected) => {
    const result = evaluateAlignment(value, unit, direction)
    expect(result.error).toBe('')
    expect(result.result).toBe(expected)
  })

  it.each([
    ['', '4096'], ['-1', '4096'], ['1.5', '4096'], ['1000000001', '4096'],
    ['4097', '0'], ['4097', ''], ['4097', '-1'], ['4097', '65537'],
  ])('拒绝无效数值 %s / %s，不产生 NaN 或除零结果', (value, unit) => {
    const result = evaluateAlignment(value, unit, 'down')
    expect(result.error).toBeTruthy()
    expect(result.result).toBeUndefined()
  })
})
