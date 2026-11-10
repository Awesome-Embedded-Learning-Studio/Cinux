// IPv4 头部判别的五道关:纯计算,判定顺序照 kernel/net/net_layout.cpp。
//
// ParseIpv4 的五个 if 按顺序排,第一条不满足就 return,后面的判断因此
// 看不到。这些判据是:实到长度够最小头、版本是 4、头部字数够 5、实到
// 长度够头部字节数、总长不小于头部字节数。

export type GateStatus = 'pass' | 'fail' | 'masked'

export interface Ipv4Gate {
  key: string
  label: string
  detail: string
  status: GateStatus
}

export type Ipv4Error =
  | ''
  | 'kDatagramTooShort'
  | 'kBadVersion'
  | 'kBadHeaderWords'
  | 'kBadTotalLength'

export interface Ipv4Judgement {
  version: number
  headerWords: number
  headerBytes: number
  received: number
  totalLength: number
  gates: Ipv4Gate[]
  error: Ipv4Error
  /** 通过时的一句话结论 */
  verdict: string
}

const MIN_HEADER_BYTES = 20
const WORD_BYTES = 4

export function judgeIpv4(firstByte: number, received: number, totalLength: number): Ipv4Judgement {
  const version = (firstByte >> 4) & 0x0f
  const headerWords = firstByte & 0x0f
  const headerBytes = headerWords * WORD_BYTES

  const raw: Omit<Ipv4Gate, 'status'>[] = [
    {
      key: 'too-short',
      label: '实到长度够最小头',
      detail: `实到 ${received} 字节,最小 20 字节`,
    },
    {
      key: 'version',
      label: '版本是 4',
      detail: `首字节高四位是 ${version},要求 4`,
    },
    {
      key: 'header-words',
      label: '头部字数够 5',
      detail: `低四位是 ${headerWords},要求不小于 5`,
    },
    {
      key: 'header-bytes',
      label: '实到长度够头部字节数',
      detail: `头部 ${headerBytes} 字节,实到 ${received} 字节`,
    },
    {
      key: 'total-length',
      label: '总长不小于头部',
      detail: `总长 ${totalLength} 字节,头部 ${headerBytes} 字节`,
    },
  ]

  const results: boolean[] = [
    received >= MIN_HEADER_BYTES,
    version === 4,
    headerWords >= MIN_HEADER_BYTES / WORD_BYTES,
    received >= headerBytes,
    totalLength >= headerBytes,
  ]
  const errors: Ipv4Error[] = [
    'kDatagramTooShort',
    'kBadVersion',
    'kBadHeaderWords',
    'kDatagramTooShort',
    'kBadTotalLength',
  ]

  const gates: Ipv4Gate[] = []
  let error: Ipv4Error = ''
  let stopped = false
  raw.forEach((gate, i) => {
    if (stopped) {
      gates.push({ ...gate, status: 'masked' })
      return
    }
    if (results[i]) {
      gates.push({ ...gate, status: 'pass' })
      return
    }
    gates.push({ ...gate, status: 'fail' })
    error = errors[i]
    stopped = true
  })

  return {
    version,
    headerWords,
    headerBytes,
    received,
    totalLength,
    gates,
    error,
    verdict: error
      ? `在「${gates.find(g => g.status === 'fail')!.label}」这一关停下,返回 ${error}`
      : `五关全过,头部 ${headerBytes} 字节`,
  }
}

/** 首字节的两个半字节,给控件显示用 */
export function nibbles(firstByte: number): { version: number, words: number } {
  return { version: (firstByte >> 4) & 0x0f, words: firstByte & 0x0f }
}
