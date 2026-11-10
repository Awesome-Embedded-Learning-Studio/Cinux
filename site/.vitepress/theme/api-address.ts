// 虚拟地址的四级索引:纯计算,组件只负责显示。
//
// 位宽与低位取自 kernel/mm/page_walk.hpp 的 IndexRange 常量(39/30/21/12,
// 每级 9 位)。大页的页内偏移宽度等于该级索引的 low,所以切到 2 MiB 时
// 页表那级消失,切到 1 GiB 时页目录也消失。

export type PageSize = 4096 | 2097152 | 1073741824

export interface AddressLevel {
  /** 显示名 */
  name: string
  /** 该级索引的起始位 */
  low: number
  /** 索引位数,固定 9 */
  width: number
  /** 本地址在该级的索引值 */
  index: number
}

export interface AddressDecode {
  address: bigint
  pageSize: PageSize
  levels: AddressLevel[]
  /** 页内偏移宽度,等于叶子级的 low */
  offsetBits: number
  offset: bigint
  /** 地址是否落在内核认的低 48 位里 */
  canonical: boolean
  note: string
}

const ADDRESS_BITS = 48
const INDEX_WIDTH = 9
const INDEX_MASK = 0x1ffn

const LEVELS = [
  { name: 'PML4', low: 39 },
  { name: 'PDPT', low: 30 },
  { name: '页目录', low: 21 },
  { name: '页表', low: 12 },
] as const

const PAGE_LABEL: Record<PageSize, string> = {
  4096: '4 KiB',
  2097152: '2 MiB',
  1073741824: '1 GiB',
}

export function pageLabel(size: PageSize): string {
  return PAGE_LABEL[size]
}

/** 叶子级:4 KiB 落在页表,2 MiB 落在页目录,1 GiB 落在 PDPT */
function leafOf(size: PageSize): number {
  if (size === 1073741824) return 30
  if (size === 2097152) return 21
  return 12
}

/** 解析用户输入:支持 0x 前缀与十进制,下划线当分隔符忽略 */
export function parseAddress(text: string): bigint | null {
  const trimmed = text.trim().replace(/_/g, '')
  if (!trimmed) return null
  try {
    if (/^0x[0-9a-f]+$/i.test(trimmed)) return BigInt(trimmed)
    if (/^[0-9]+$/.test(trimmed)) return BigInt(trimmed)
  } catch {
    return null
  }
  return null
}

export function decodeAddress(address: bigint, pageSize: PageSize): AddressDecode {
  const leafLow = leafOf(pageSize)
  const levels: AddressLevel[] = LEVELS
    .filter(level => level.low >= leafLow)
    .map(level => ({
      name: level.name,
      low: level.low,
      width: INDEX_WIDTH,
      index: Number((address >> BigInt(level.low)) & INDEX_MASK),
    }))
  // 64 位地址的合法形状是高 16 位为 0 或全 1,x86-64 只要求符号扩展
  const high = address >> 48n
  const canonical = address >= 0n && (high === 0n || high === 0xffffn)
  return {
    address,
    pageSize,
    levels,
    offsetBits: leafLow,
    offset: address & ((1n << BigInt(leafLow)) - 1n),
    canonical,
    note: canonical ? '' : '高 16 位既不是全 0 也不是全 1,这不是合法的 64 位地址',
  }
}

/** 比特区间的文字表示,例如 47:39 */
export function rangeText(level: AddressLevel): string {
  const high = level.low + level.width - 1
  return `${high}:${level.low}`
}

export function hex(address: bigint): string {
  return `0x${address.toString(16).padStart(16, '0')}`
}
