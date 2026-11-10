<script setup lang="ts">
import { computed, ref } from 'vue'
import { judgeIpv4, nibbles } from '../api-ipv4'

const firstByte = ref('0x45')
const received = ref('20')
const totalLength = ref('20')

const byteValue = computed(() => {
  const text = firstByte.value.trim().toLowerCase().replace(/^0x/, '')
  if (!/^[0-9a-f]{1,2}$/.test(text)) return null
  return parseInt(text, 16)
})
const receivedValue = computed(() => (/^\d+$/.test(received.value.trim()) ? Number(received.value.trim()) : null))
const totalValue = computed(() => (/^\d+$/.test(totalLength.value.trim()) ? Number(totalLength.value.trim()) : null))

const valid = computed(() => byteValue.value !== null && receivedValue.value !== null && totalValue.value !== null)
const result = computed(() => (valid.value ? judgeIpv4(byteValue.value!, receivedValue.value!, totalValue.value!) : null))
const nib = computed(() => (byteValue.value === null ? null : nibbles(byteValue.value)))

const presets = [
  { label: '0x45 正常', byte: '0x45', received: '20', total: '20' },
  { label: '0x65 版本错', byte: '0x65', received: '20', total: '20' },
  { label: '0x44 字数不够', byte: '0x44', received: '20', total: '20' },
  { label: '0x46 实到不足', byte: '0x46', received: '22', total: '22' },
  { label: '总长太小', byte: '0x45', received: '20', total: '10' },
]
function apply(preset: typeof presets[number]) {
  firstByte.value = preset.byte
  received.value = preset.received
  totalLength.value = preset.total
}
</script>

<template>
  <div class="api-lab">
    <div class="api-lab__bar">
      <label class="api-lab__field">
        <span>首字节</span>
        <input v-model="firstByte" type="text" spellcheck="false" aria-label="首字节,十六进制">
      </label>
      <label class="api-lab__field">
        <span>实到字节</span>
        <input v-model="received" type="text" inputmode="numeric" aria-label="实际收到的字节数">
      </label>
      <label class="api-lab__field">
        <span>总长字段</span>
        <input v-model="totalLength" type="text" inputmode="numeric" aria-label="头部里的总长字段">
      </label>
    </div>
    <div class="api-lab__presets">
      <button v-for="preset in presets" :key="preset.label" type="button" @click="apply(preset)">{{ preset.label }}</button>
    </div>

    <p v-if="!result" class="api-lab__error" role="alert">首字节写 0x 开头的两位十六进制,长度写十进制数字。</p>
    <template v-else>
      <div class="api-lab__split">
        <div class="api-lab__cell"><span>版本</span><strong>{{ nib!.version }}</strong></div>
        <div class="api-lab__cell"><span>头部字数</span><strong>{{ nib!.words }}</strong></div>
        <div class="api-lab__cell"><span>头部字节</span><strong>{{ result.headerBytes }}</strong></div>
        <div class="api-lab__cell"><span>总长</span><strong>{{ result.totalLength }}</strong></div>
      </div>

      <ol class="api-lab__gates">
        <li v-for="gate in result.gates" :key="gate.key" :data-status="gate.status">
          <span class="api-lab__gate-mark" aria-hidden="true">{{ gate.status === 'pass' ? '✓' : gate.status === 'fail' ? '✕' : '–' }}</span>
          <span class="api-lab__gate-body">
            <strong>{{ gate.label }}</strong>
            <small>{{ gate.detail }}</small>
          </span>
          <span class="api-lab__gate-tag">{{ gate.status === 'pass' ? '通过' : gate.status === 'fail' ? '停下' : '没轮到' }}</span>
        </li>
      </ol>
      <p class="api-lab__verdict" :data-status="result.error ? 'fail' : 'pass'">{{ result.verdict }}</p>
    </template>
    <p class="api-lab__source">判定顺序照 kernel/net/net_layout.cpp 的 ParseIpv4,五条 if 依次排,前一条停下后面的就不再执行。</p>
  </div>
</template>
