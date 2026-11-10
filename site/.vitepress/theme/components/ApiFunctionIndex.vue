<script setup lang="ts">
import { computed, ref } from 'vue'

interface IndexItem {
  name: string
  anchor: string
  summary: string
  category: string
  /** 函数以外还有变量、枚举与类型别名,符号标记与括号后缀跟着变 */
  kind?: string
}
const props = defineProps<{ items: IndexItem[] }>()
const category = ref('全部')
const categories = computed(() => ['全部', ...new Set(props.items.map(i => i.category))])
const filtered = computed(() => category.value === '全部'
  ? props.items : props.items.filter(i => i.category === category.value))

const GLYPH: Record<string, string> = {
  function: 'ƒ',
  enum: '≡',
  typedef: '◇',
  variable: '𝑣',
}
const glyphOf = (item: IndexItem) => GLYPH[item.kind ?? 'function'] ?? 'ƒ'
const isFunction = (item: IndexItem) => (item.kind ?? 'function') === 'function'
</script>

<template>
  <div class="api-function-index">
    <div class="api-function-index__bar">
      <div class="api-function-index__filters" role="group" aria-label="按用途筛选接口">
        <button v-for="item in categories" :key="item" type="button" :aria-pressed="category === item" @click="category = item">{{ item }}</button>
      </div>
      <span class="api-function-index__count" aria-live="polite">{{ filtered.length }} 个接口</span>
    </div>
    <div class="api-function-index__grid">
      <a v-for="item in filtered" :key="item.anchor" :href="`#${item.anchor}`" class="api-function-index__item">
        <span class="api-function-index__symbol" :data-kind="item.kind ?? 'function'" aria-hidden="true">{{ glyphOf(item) }}</span>
        <span>
          <strong>{{ item.name }}<span v-if="isFunction(item)" class="api-function-index__parens">()</span></strong>
          <small>{{ item.summary }}</small>
        </span>
        <span class="api-function-index__arrow" aria-hidden="true">↗</span>
      </a>
    </div>
  </div>
</template>
