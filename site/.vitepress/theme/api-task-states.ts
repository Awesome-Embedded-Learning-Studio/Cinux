// 任务状态与迁移:数据表,组件只负责显示与筛选。
//
// 状态名取自 kernel/proc/task.hpp 的 TaskState,迁移取自
// kernel/proc/scheduler.cpp 里每一处给 state 赋值的地方。僵尸被父进程
// wait 收走时直接离开任务表,没有新的状态值,所以表里用「回收」表示。

export interface TaskStateInfo {
  key: string
  label: string
  meaning: string
}

export interface TaskEdge {
  from: string
  to: string
  /** 做这件事的函数 */
  via: string
  /** 源码行,形如 scheduler.cpp:224 */
  where: string
  /** 这一步的附加条件 */
  caveat?: string
}

export const TASK_STATES: TaskStateInfo[] = [
  { key: 'kReady', label: '就绪', meaning: '在队列里等一个 CPU' },
  { key: 'kRunning', label: '运行', meaning: '正在 CPU 上跑' },
  { key: 'kBlocked', label: '阻塞', meaning: '挂在等待队列上,不叫醒就排不上' },
  { key: 'kStopped', label: '停止', meaning: '被停止信号钉住,只有 SIGCONT 或 SIGKILL 能拉回' },
  { key: 'kZombie', label: '僵尸', meaning: '已退出但父进程还在,尸体留着等回收' },
  { key: 'kDead', label: '已死', meaning: '函数返回并退出,内存等着回收' },
]

/** 离开任务表的终点,不是 TaskState 的取值 */
export const RECLAIM = '回收'

export const TASK_EDGES: TaskEdge[] = [
  {
    from: 'kBlocked', to: 'kReady', via: 'Scheduler::seat',
    where: 'scheduler.cpp:127', caveat: '队列满时新任务留在阻塞,返回值报假',
  },
  {
    from: 'kReady', to: 'kRunning', via: 'pick_next',
    where: 'scheduler.cpp:224', caveat: '切换路径有多处,都是同一件事',
  },
  {
    from: 'kRunning', to: 'kReady', via: 'Scheduler::yield',
    where: 'scheduler.cpp:203',
  },
  {
    from: 'kRunning', to: 'kReady', via: 'Scheduler::maybe_preempt',
    where: 'scheduler.cpp:316', caveat: '只在中断里且抢占开着时发生',
  },
  {
    from: 'kRunning', to: 'kBlocked', via: 'Scheduler::prepare_to_wait',
    where: 'scheduler.cpp:498',
  },
  {
    from: 'kBlocked', to: 'kReady', via: 'Scheduler::unblock',
    where: 'scheduler.cpp:553', caveat: '队列满时这次叫醒被丢掉,任务留在阻塞',
  },
  {
    from: 'kRunning', to: 'kStopped', via: 'Scheduler::stop_current',
    where: 'scheduler.cpp:563',
  },
  {
    from: 'kStopped', to: 'kReady', via: 'Scheduler::resume',
    where: 'scheduler.cpp:577', caveat: '队列满时推迟,任务留在停止',
  },
  {
    from: 'kReady', to: 'kStopped', via: 'Scheduler::withdraw',
    where: 'scheduler.cpp:588', caveat: '只从队列里摘,不着手别的清理',
  },
  {
    from: 'kRunning', to: 'kZombie', via: 'Scheduler::exit_current',
    where: 'scheduler.cpp:367', caveat: '有父进程且不是线程成员才留尸',
  },
  {
    from: 'kRunning', to: 'kDead', via: 'Scheduler::exit_current',
    where: 'scheduler.cpp:377', caveat: '没有父进程,直接标死等回收',
  },
  {
    from: 'kZombie', to: RECLAIM, via: 'take_zombie_child 后回收',
    where: 'scheduler.cpp:394', caveat: '父进程 wait 到才发生',
  },
  {
    from: 'kDead', to: RECLAIM, via: 'Scheduler::reap_pending',
    where: 'scheduler.cpp:614', caveat: '等它彻底离开 CPU',
  },
]

export function edgesOf(state: string): { outgoing: TaskEdge[], incoming: TaskEdge[] } {
  return {
    outgoing: TASK_EDGES.filter(edge => edge.from === state),
    incoming: TASK_EDGES.filter(edge => edge.to === state),
  }
}

export function stateLabel(key: string): string {
  return TASK_STATES.find(s => s.key === key)?.label ?? (key === RECLAIM ? RECLAIM : key)
}
