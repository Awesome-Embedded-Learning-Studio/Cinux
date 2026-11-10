<script setup lang="ts">
import { computed, ref } from 'vue'
import { evaluateAlignment, type AlignmentDirection } from '../math-playground'
import { useCopyFeedback } from '../copy-feedback'

const direction = ref<AlignmentDirection>('down')
const value = ref('4097')
const unit = ref('4096')
const evaluation = computed(() => evaluateAlignment(value.value, unit.value, direction.value))
const calculation = computed(() => evaluation.value.error ? null : evaluation.value)
const fn = computed(() => direction.value === 'down' ? 'AlignDown' : 'AlignUp')
const position = computed(() => calculation.value ? calculation.value.offset / calculation.value.unit * 100 : 0)
const { feedback, copy } = useCopyFeedback()
const presets = [
  { name: '跨一页', value: '4097', unit: '4096' },
  { name: '扇区边界', value: '1000', unit: '512' },
  { name: '已对齐', value: '8192', unit: '4096' },
]
function preset(item: typeof presets[number]) { value.value = item.value; unit.value = item.unit }
function inputText(event: Event) { return (event.target as HTMLInputElement).value }
function slide(event: Event) {
  if (calculation.value) value.value = String(calculation.value.lower + Number((event.target as HTMLInputElement).value))
}
function decimal(n: number) { return n.toLocaleString('en-US') }
function hex(n: number) { return `0x${n.toString(16).toUpperCase()}` }
</script>

<template>
  <section class="api-playground" aria-label="地址对齐交互演示">
    <div class="api-playground__top">
      <div class="api-playground__intro"><span class="api-playground__live"><span /> LIVE</span><strong>让边界看得见</strong><span>改一个数，看看地址落在哪里。</span></div>
      <div class="api-playground__modes" role="group" aria-label="对齐方向">
        <button type="button" :aria-pressed="direction === 'down'" @click="direction = 'down'">↓ 向下</button>
        <button type="button" :aria-pressed="direction === 'up'" @click="direction = 'up'">↑ 向上</button>
      </div>
    </div>
    <div class="api-playground__workspace">
      <div class="api-playground__inputs">
        <label>待对齐数值 <span>value</span><input :value="value" type="number" min="0" max="1000000000" step="1" inputmode="numeric" :aria-invalid="!!evaluation.error" :aria-describedby="evaluation.error ? 'alignment-error' : undefined" @input="value = inputText($event)" /></label>
        <label>对齐单位 <span>unit</span><input :value="unit" type="number" min="1" max="65536" step="1" inputmode="numeric" :aria-invalid="!!evaluation.error" :aria-describedby="evaluation.error ? 'alignment-error' : undefined" @input="unit = inputText($event)" /></label>
        <div class="api-playground__presets"><span>试试</span><button v-for="item in presets" :key="item.name" type="button" :aria-pressed="String(value) === item.value && String(unit) === item.unit" @click="preset(item)">{{ item.name }}</button></div>
      </div>
      <div class="api-playground__result" aria-live="polite" aria-atomic="true">
        <span>{{ fn }}<span class="api-playground__result-parens">(value, unit)</span></span>
        <template v-if="calculation">
          <div class="api-playground__number">{{ decimal(calculation.result) }}<span>{{ hex(calculation.result) }}</span></div>
          <span class="api-playground__delta">{{ calculation.aligned ? '✓ 已在边界，无需移动' : `${direction === 'down' ? '向下' : '向上'}移动 ${decimal(calculation.difference)}` }}</span>
          <button type="button" class="api-playground__copy" @click="copy(String(calculation.result))">{{ feedback || '复制结果 ↗' }}</button>
        </template>
        <p v-else class="api-playground__invalid-result">等待有效输入</p>
      </div>
    </div>
    <p v-if="evaluation.error" id="alignment-error" class="api-playground__error" role="alert">{{ evaluation.error }}</p>
    <div v-if="calculation" class="api-playground__visual">
      <div class="api-playground__legend"><span><i class="api-playground__legend-value" />输入位置</span><span><i class="api-playground__legend-result" />对齐边界</span><span class="api-playground__scale-note">每格 {{ decimal(calculation.unit) }} · 拖动滑块探索</span></div>
      <div class="api-playground__track" role="img" :aria-label="`输入 ${calculation.value} 位于 ${calculation.lower} 与 ${calculation.upper} 之间，${fn} 返回 ${calculation.result}`">
        <span class="api-playground__boundary api-playground__boundary--start" :class="{ 'is-result': calculation.result === calculation.lower }"><b>{{ decimal(calculation.lower) }}</b></span>
        <span class="api-playground__boundary api-playground__boundary--end" :class="{ 'is-result': calculation.result === calculation.upper }"><b>{{ decimal(calculation.upper) }}</b></span>
        <span class="api-playground__travel" :style="{ left: `${direction === 'down' || calculation.aligned ? 0 : position}%`, width: `${direction === 'down' || calculation.aligned ? position : 100 - position}%` }" />
        <span class="api-playground__marker" :style="{ left: `${position}%` }" :class="{ 'is-near-start': position < 18, 'is-near-end': position > 82 }"><b>{{ decimal(calculation.value) }}<small>输入</small></b></span>
      </div>
      <input class="api-playground__slider" type="range" aria-label="调整数值在对齐单元内的位置" min="0" :max="Math.min(calculation.unit - 1, 1_000_000_000 - calculation.lower)" :value="calculation.offset" @input="slide" />
    </div>
    <div class="api-playground__foot"><span aria-hidden="true">↳</span> <code>{{ fn }}({{ value }}, {{ unit }})</code><span v-if="calculation">→ <strong>{{ calculation.result }}</strong></span><a href="#reference">查看接口约束 ↓</a></div>
  </section>
</template>
