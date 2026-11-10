<script setup lang="ts">
import { ref, useSlots } from 'vue'
import { withBase } from 'vitepress'
import { useCopyFeedback } from '../copy-feedback'

defineProps<{
  name: string, anchor: string, namespace: string, summary: string, qualifier?: string,
  since: string, source: string, sourceLabel: string,
  callers: { name: string, href: string }[],
}>()
const slots = useSlots()
const selected = ref('declaration')
const panel = ref<HTMLElement>()
const { feedback, copy } = useCopyFeedback()
const tabs = () => slots.example ? ['declaration', 'example'] : ['declaration']

function selectWithKeyboard(event: KeyboardEvent) {
  const keys = ['ArrowLeft', 'ArrowRight', 'Home', 'End']
  if (!keys.includes(event.key)) return
  event.preventDefault()
  const values = tabs()
  const index = values.indexOf(selected.value)
  const next = event.key === 'Home' ? 0 : event.key === 'End' ? values.length - 1
    : (index + (event.key === 'ArrowRight' ? 1 : -1) + values.length) % values.length
  selected.value = values[next]
  const list = (event.currentTarget as HTMLElement).parentElement
  list?.querySelectorAll<HTMLButtonElement>('[role="tab"]')[next]?.focus()
}

function copyCode() {
  const code = panel.value?.querySelector<HTMLElement>(`[data-panel="${selected.value}"] pre code`)
  if (code) void copy(code.innerText)
}
</script>

<template>
  <article class="api-function" :aria-labelledby="anchor">
    <header class="api-function__header">
      <div>
        <div class="api-function__namespace">{{ namespace }}</div>
        <div class="api-function__title"><slot name="heading" /><span v-if="qualifier" class="api-function__qualifier">{{ qualifier }}</span></div>
        <p class="api-function__summary">{{ summary }}</p>
      </div>
      <a v-if="source" class="api-function__source" :href="source" target="_blank" rel="noopener noreferrer" :aria-label="`查看 ${name} 的源码`" :title="sourceLabel">源码 <span aria-hidden="true">↗</span></a>
    </header>

    <div ref="panel" class="api-function__code">
      <div class="api-function__toolbar">
        <div role="tablist" :aria-label="`${name} 代码`">
          <button v-for="tab in tabs()" :id="`${anchor}-${tab}-tab`" :key="tab" type="button" role="tab" :aria-selected="selected === tab" :tabindex="selected === tab ? 0 : -1" :aria-controls="`${anchor}-${tab}-panel`" @click="selected = tab" @keydown="selectWithKeyboard">{{ tab === 'declaration' ? '函数声明' : '使用示例' }}</button>
        </div>
        <button class="api-function__copy" type="button" :aria-label="`复制 ${name} 的${selected === 'declaration' ? '声明' : '示例'}`" @click="copyCode">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true"><rect x="8" y="8" width="12" height="13" rx="2" /><path d="M16 8V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h3" /></svg>
          <span aria-live="polite">{{ feedback || '复制' }}</span>
        </button>
      </div>
      <div v-for="tab in tabs()" v-show="selected === tab" :id="`${anchor}-${tab}-panel`" :key="tab" :data-panel="tab" role="tabpanel" :aria-labelledby="`${anchor}-${tab}-tab`" tabindex="0"><slot :name="tab" /></div>
    </div>

    <div class="api-function__body">
      <div class="api-function__description"><slot /></div>
      <div v-if="slots.parameters" class="api-function__parameters"><div class="api-function__label">参数 <span>PARAMETERS</span></div><slot name="parameters" /></div>
      <div v-if="slots.returns" class="api-function__returns"><span class="api-function__label">↳ 返回值</span><div><slot name="returns" /></div></div>
      <div v-if="slots.notes" class="api-function__note"><span class="api-function__note-icon" aria-hidden="true">i</span><div><slot name="notes" /></div></div>
      <div v-if="slots.warning" class="api-function__warning"><strong>注意</strong><slot name="warning" /></div>
    </div>

    <footer class="api-function__footer">
      <details v-if="callers.length" class="api-function__callers">
        <summary><span class="api-function__chevron" aria-hidden="true">›</span>调用关系 <span class="api-function__caller-count">{{ callers.length }}</span></summary>
        <div class="api-function__caller-list">
          <template v-for="caller in callers" :key="caller.name + caller.href">
            <a v-if="caller.href" :href="withBase(caller.href)"><code>{{ caller.name }}</code><span aria-hidden="true">↗</span></a>
            <span v-else><code>{{ caller.name }}</code></span>
          </template>
        </div>
      </details>
      <span v-if="since" class="api-function__since">自 <code>{{ since }}</code></span>
    </footer>
  </article>
</template>
