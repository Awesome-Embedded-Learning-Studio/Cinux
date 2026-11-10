<script setup lang="ts">
import { computed, ref } from 'vue'
import { decodeAddress, hex, parseAddress, rangeText, type PageSize } from '../api-address'

const addressText = ref('0x00007fffffffffff')
const pageSize = ref<PageSize>(4096)
const sizes: { value: PageSize, label: string }[] = [
  { value: 4096, label: '4 KiB' },
  { value: 2097152, label: '2 MiB' },
  { value: 1073741824, label: '1 GiB' },
]

const parsed = computed(() => parseAddress(addressText.value))
const result = computed(() => (parsed.value === null ? null : decodeAddress(parsed.value, pageSize.value)))
const presets = ['0x0000000000401000', '0x00007fffffffffff', '0xffff800000000000']

const leafNote = computed(() => {
  if (pageSize.value === 4096) return '4 KiB 叶子停在页表,四级都用上,页内偏移 12 位。'
  if (pageSize.value === 2097152) return '2 MiB 叶子停在页目录,页表那级并进偏移,页内偏移 21 位,不再有页表索引。'
  return '1 GiB 叶子停在 PDPT,页目录与页表两级都并进偏移,页内偏移 30 位。'
})
</script>

<template>
  <div class="api-lab">
    <div class="api-lab__bar">
      <label class="api-lab__field">
        <span>虚拟地址</span>
        <input v-model="addressText" type="text" inputmode="text" spellcheck="false" aria-label="虚拟地址,支持 0x 前缀">
      </label>
      <div class="api-lab__modes" role="group" aria-label="页面大小">
        <button
          v-for="size in sizes"
          :key="size.value"
          type="button"
          :aria-pressed="pageSize === size.value"
          @click="pageSize = size.value"
        >{{ size.label }}</button>
      </div>
    </div>
    <div class="api-lab__presets">
      <button v-for="preset in presets" :key="preset" type="button" @click="addressText = preset">{{ preset }}</button>
    </div>

    <p v-if="!result" class="api-lab__error" role="alert">这个地址读不出来,用 0x 前缀的十六进制或十进制数字。</p>
    <template v-else>
      <table class="api-lab__table">
        <thead>
          <tr><th>层级</th><th>比特区间</th><th>索引</th></tr>
        </thead>
        <tbody>
          <tr v-for="level in result.levels" :key="level.name">
            <td>{{ level.name }}</td>
            <td class="api-lab__mono">{{ rangeText(level) }}</td>
            <td class="api-lab__mono">{{ level.index }}</td>
          </tr>
          <tr>
            <td>页内偏移</td>
            <td class="api-lab__mono">{{ result.offsetBits - 1 }}:0</td>
            <td class="api-lab__mono">{{ hex(result.offset) }}</td>
          </tr>
        </tbody>
      </table>
      <p class="api-lab__note">
        {{ leafNote }}
        <template v-if="result.note">{{ result.note }}。</template>
      </p>
    </template>
    <p class="api-lab__source">索引常量取自 kernel/mm/page_walk.hpp 的 IndexRange。这里只算索引,不遍历页表,也不拼物理地址。</p>
  </div>
</template>
