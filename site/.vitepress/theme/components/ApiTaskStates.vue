<script setup lang="ts">
import { computed, ref } from 'vue'
import { RECLAIM, TASK_EDGES, TASK_STATES, edgesOf, stateLabel } from '../api-task-states'

const selected = ref('kRunning')
const current = computed(() => edgesOf(selected.value))
const state = computed(() => TASK_STATES.find(s => s.key === selected.value) ?? null)
const reclaimEdges = computed(() => TASK_EDGES.filter(e => e.to === RECLAIM))
</script>

<template>
  <div class="api-lab">
    <div class="api-lab__modes" role="group" aria-label="任务状态">
      <button
        v-for="item in TASK_STATES"
        :key="item.key"
        type="button"
        :aria-pressed="selected === item.key"
        @click="selected = item.key"
      >{{ item.key }}</button>
    </div>

    <p v-if="state" class="api-lab__note">
      <strong>{{ state.key }} · {{ state.label }}</strong>{{ state.meaning }}。
    </p>

    <div class="api-lab__columns">
      <section>
        <h4>进来</h4>
        <ul v-if="current.incoming.length" class="api-lab__edges">
          <li v-for="edge in current.incoming" :key="`in-${edge.from}-${edge.via}`">
            <span class="api-lab__from">{{ edge.from }}</span>
            <span class="api-lab__arrow" aria-hidden="true">→</span>
            <strong>{{ edge.via }}</strong>
            <code>{{ edge.where }}</code>
            <small v-if="edge.caveat">{{ edge.caveat }}</small>
          </li>
        </ul>
        <p v-else class="api-lab__empty">没有别的状态直接进到这里。</p>
      </section>
      <section>
        <h4>出去</h4>
        <ul v-if="current.outgoing.length" class="api-lab__edges">
          <li v-for="edge in current.outgoing" :key="`out-${edge.to}-${edge.via}`">
            <span class="api-lab__arrow" aria-hidden="true">→</span>
            <span class="api-lab__from">{{ edge.to === '回收' ? '回收' : stateLabel(edge.to) }}</span>
            <strong>{{ edge.via }}</strong>
            <code>{{ edge.where }}</code>
            <small v-if="edge.caveat">{{ edge.caveat }}</small>
          </li>
        </ul>
        <p v-else class="api-lab__empty">从这里没有直接离开的路。</p>
      </section>
    </div>

    <p class="api-lab__note">
      僵尸与已死最后都走「回收」离开任务表,回收不是 TaskState 的取值。
      回收的 {{ reclaimEdges.length }} 条路分别是父进程 wait 与 reap_pending。
    </p>
    <p class="api-lab__source">状态名与源码行取自 kernel/proc/task.hpp 和 kernel/proc/scheduler.cpp,每条边对应一处 state 赋值。</p>
  </div>
</template>
