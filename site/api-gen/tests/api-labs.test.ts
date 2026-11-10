import { describe, expect, it } from 'vitest'
import { decodeAddress, parseAddress, rangeText } from '../../.vitepress/theme/api-address'
import { judgeIpv4 } from '../../.vitepress/theme/api-ipv4'
import { RECLAIM, TASK_EDGES, TASK_STATES, edgesOf, stateLabel } from '../../.vitepress/theme/api-task-states'

describe('地址拆解', () => {
  it('4 KiB 页取四级索引与 12 位偏移', () => {
    const r = decodeAddress(parseAddress('0x00007fffffffffff')!, 4096)
    expect(r.levels.map(l => l.name)).toEqual(['PML4', 'PDPT', '页目录', '页表'])
    expect(r.levels.map(l => l.index)).toEqual([255, 511, 511, 511])
    expect(r.offsetBits).toBe(12)
    expect(r.offset).toBe(0xfffn)
    expect(rangeText(r.levels[0])).toBe('47:39')
  })

  it('2 MiB 页去掉页表那级,偏移变 21 位', () => {
    const r = decodeAddress(parseAddress('0x401000')!, 2097152)
    expect(r.levels.map(l => l.name)).toEqual(['PML4', 'PDPT', '页目录'])
    expect(r.offsetBits).toBe(21)
    expect(r.offset).toBe(0x1000n)
  })

  it('1 GiB 页只剩两级,偏移变 30 位', () => {
    const r = decodeAddress(parseAddress('0x401000')!, 1073741824)
    expect(r.levels.map(l => l.name)).toEqual(['PML4', 'PDPT'])
    expect(r.offsetBits).toBe(30)
    expect(r.offset).toBe(0x401000n)
  })

  it('内核半区地址是合法的符号扩展', () => {
    const r = decodeAddress(parseAddress('0xffff800000000000')!, 4096)
    expect(r.canonical).toBe(true)
    expect(r.levels[0].index).toBe(256)
  })

  it('高 16 位既不全 0 也不全 1 时标出非法', () => {
    const r = decodeAddress(parseAddress('0x1234800000000000')!, 4096)
    expect(r.canonical).toBe(false)
    expect(r.note).not.toBe('')
  })

  it('读不出来的输入返回 null', () => {
    expect(parseAddress('')).toBeNull()
    expect(parseAddress('zzz')).toBeNull()
    expect(parseAddress('0x')).toBeNull()
    expect(parseAddress('0x2a')).toBe(42n)
  })
})

describe('IPv4 头部判别', () => {
  it('0x45 配 20 字节全过', () => {
    const r = judgeIpv4(0x45, 20, 20)
    expect(r.error).toBe('')
    expect(r.gates.every(g => g.status === 'pass')).toBe(true)
    expect(r.headerBytes).toBe(20)
  })

  it('版本不对时在第二关停下,后面三个没轮到', () => {
    const r = judgeIpv4(0x65, 20, 20)
    expect(r.error).toBe('kBadVersion')
    expect(r.gates.map(g => g.status)).toEqual(['pass', 'fail', 'masked', 'masked', 'masked'])
  })

  it('头部字数不够时在第三关停下', () => {
    const r = judgeIpv4(0x44, 20, 20)
    expect(r.error).toBe('kBadHeaderWords')
  })

  it('实到长度不够头部字节数时报同一个 too-short', () => {
    const r = judgeIpv4(0x46, 22, 22)
    expect(r.error).toBe('kDatagramTooShort')
    expect(r.gates[3].status).toBe('fail')
    expect(r.gates[4].status).toBe('masked')
  })

  it('总长比头部还小时报 bad-total-length', () => {
    const r = judgeIpv4(0x45, 20, 10)
    expect(r.error).toBe('kBadTotalLength')
  })

  it('实到不足最短头时第一关就停', () => {
    const r = judgeIpv4(0x45, 19, 20)
    expect(r.error).toBe('kDatagramTooShort')
    expect(r.gates[1].status).toBe('masked')
  })
})

describe('任务状态与迁移', () => {
  const known = new Set([...TASK_STATES.map(s => s.key), RECLAIM])

  it('每条边的两端都是已知状态或回收', () => {
    for (const edge of TASK_EDGES) {
      expect(known.has(edge.from)).toBe(true)
      expect(known.has(edge.to)).toBe(true)
    }
  })

  it('停止状态有两条进来的路,出去的只有 resume', () => {
    const { incoming, outgoing } = edgesOf('kStopped')
    expect(incoming.map(e => e.via)).toEqual(['Scheduler::stop_current', 'Scheduler::withdraw'])
    expect(outgoing.map(e => e.via)).toEqual(['Scheduler::resume'])
    expect(outgoing[0].caveat).toContain('队列满')
  })

  it('就绪状态能进能出,退出路径留在阻塞上的丢醒来在备注里', () => {
    const { incoming, outgoing } = edgesOf('kReady')
    expect(incoming.some(e => e.via === 'Scheduler::unblock')).toBe(true)
    expect(outgoing.some(e => e.via === 'Scheduler::withdraw')).toBe(true)
  })

  it('僵尸与已死都只有一条出口', () => {
    expect(edgesOf('kZombie').outgoing).toHaveLength(1)
    expect(edgesOf('kDead').outgoing).toHaveLength(1)
    expect(edgesOf('kZombie').outgoing[0].to).toBe(RECLAIM)
  })

  it('状态名能翻成中文标签', () => {
    expect(stateLabel('kRunning')).toBe('运行')
    expect(stateLabel(RECLAIM)).toBe(RECLAIM)
  })
})
