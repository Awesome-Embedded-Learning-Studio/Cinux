export type AlignmentDirection = 'down' | 'up'

// 有限的演示范围保证 32/64 位 unsigned long 都不会溢出。
export function evaluateAlignment(valueText: string, unitText: string, direction: AlignmentDirection) {
  if (!/^\d+$/.test(valueText) || Number(valueText) > 1_000_000_000) {
    return { error: '数值请输入 0 到 1,000,000,000 之间的整数。' } as const
  }
  if (!/^\d+$/.test(unitText) || Number(unitText) < 1 || Number(unitText) > 65_536) {
    return { error: '对齐单位请输入 1 到 65,536 之间的整数。' } as const
  }
  const value = Number(valueText)
  const unit = Number(unitText)
  const lower = Math.floor(value / unit) * unit
  const upper = lower + unit
  const aligned = value % unit === 0
  const result = direction === 'down' || aligned ? lower : upper
  return { error: '', value, unit, lower, upper, aligned, result, offset: value - lower, difference: Math.abs(result - value) } as const
}
