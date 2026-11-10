<script setup lang="ts">
/**
 * API 分组浏览器:卡片矩阵 + 即时筛选。
 *
 * 数据由 api-gen 生成首页时内联传入(组名/标题/分区/符号数/覆盖/代表接口),
 * 本组件只做展示与过滤,无网络请求。分区顺序与后端 SECTIONS 一致。
 */
import { computed, ref } from 'vue'

interface GroupCard {
  name: string
  title: string
  section: string
  total: number
  withBrief: number
  samples: string[]
}

const props = defineProps<{ groups: GroupCard[] }>()

const query = ref('')

const sectionOrder = ['Cinux-Base 基础库', '引导过程', '内核子系统', '其他']

const filtered = computed(() => {
  const q = query.value.trim().toLowerCase()
  if (!q) return props.groups
  return props.groups.filter(
    g =>
      g.name.toLowerCase().includes(q) ||
      g.title.toLowerCase().includes(q) ||
      g.samples.some(s => s.toLowerCase().includes(q)),
  )
})

const sections = computed(() => {
  return sectionOrder
    .map(sec => ({ title: sec, items: filtered.value.filter(g => g.section === sec) }))
    .filter(s => s.items.length > 0)
})

const base = (import.meta as any).env?.BASE_URL ?? '/'

function badgeType(g: GroupCard): string {
  const pct = g.total === 0 ? 100 : Math.round((g.withBrief / g.total) * 100)
  return pct >= 95 ? 'tip' : pct >= 70 ? 'info' : 'warning'
}

function link(name: string): string {
  return `${base}api/${name}/`
}
</script>

<template>
  <div class="apix">
    <div class="apix__bar">
      <input
        v-model="query"
        class="apix__input"
        type="search"
        placeholder="按分组名或接口过滤,如 ring、内存、mm…"
        aria-label="筛选 API 分组"
      />
      <span class="apix__count">{{ filtered.length }} / {{ groups.length }} 个分组</span>
    </div>

    <p v-if="filtered.length === 0" class="apix__empty">
      没有匹配的分组。试试更短的关键词,或到各分组页内找具体接口。
    </p>

    <section v-for="sec in sections" :key="sec.title" class="apix__section">
      <h3 class="apix__section-title">{{ sec.title }}</h3>
      <div class="apix__grid">
        <a v-for="g in sec.items" :key="g.name" :href="link(g.name)" class="apix__card">
          <span class="apix__card-head">
            <strong class="apix__card-title">{{ g.title }}</strong>
            <span
              class="apix__badge"
              :data-tone="badgeType(g)"
            >{{ g.total }}</span>
          </span>
          <span v-if="g.samples.length" class="apix__samples">
            <code v-for="s in g.samples" :key="s">{{ s }}</code>
          </span>
          <span class="apix__go">进入 →</span>
        </a>
      </div>
    </section>
  </div>
</template>

<style scoped>
.apix__bar {
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 4px 0 20px;
}
.apix__input {
  flex: 1;
  max-width: 420px;
  padding: 8px 12px;
  border-radius: 8px;
  border: 1px solid var(--vp-c-border);
  background: var(--vp-c-bg);
  color: var(--vp-c-text-1);
  font-size: 14px;
  transition: border-color 0.15s;
}
.apix__input:focus {
  outline: none;
  border-color: var(--vp-c-brand-1);
}
.apix__count {
  font-size: 13px;
  color: var(--vp-c-text-3);
  white-space: nowrap;
}
.apix__empty {
  color: var(--vp-c-text-2);
  padding: 24px 0;
}
.apix__section {
  margin-bottom: 28px;
}
.apix__section-title {
  font-size: 15px;
  font-weight: 600;
  color: var(--vp-c-text-2);
  margin: 0 0 12px;
  padding-bottom: 6px;
  border-bottom: 1px solid var(--vp-c-divider);
}
.apix__grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 12px;
}
.apix__card {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 14px 16px;
  border: 1px solid var(--vp-c-border);
  border-radius: 10px;
  background: var(--vp-c-bg);
  text-decoration: none;
  transition: border-color 0.15s, transform 0.15s;
}
.apix__card:hover {
  border-color: var(--vp-c-brand-1);
  transform: translateY(-1px);
}
.apix__card-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}
.apix__card-title {
  font-size: 14px;
  color: var(--vp-c-text-1);
}
.apix__badge {
  font-size: 12px;
  padding: 1px 8px;
  border-radius: 999px;
  border: 1px solid var(--vp-c-border);
  color: var(--vp-c-text-2);
}
.apix__badge[data-tone='tip'] {
  color: var(--vp-button-brand-bg);
  border-color: color-mix(in srgb, var(--vp-button-brand-bg) 40%, transparent);
}
.apix__badge[data-tone='warning'] {
  color: var(--vp-c-warning-1);
  border-color: color-mix(in srgb, var(--vp-c-warning-1) 40%, transparent);
}
.apix__samples {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
.apix__samples code {
  font-size: 11.5px;
  padding: 1px 6px;
  border-radius: 4px;
  background: var(--vp-c-default-soft);
  color: var(--vp-c-text-2);
}
.apix__go {
  font-size: 12px;
  color: var(--vp-c-brand-1);
  margin-top: auto;
}
</style>
