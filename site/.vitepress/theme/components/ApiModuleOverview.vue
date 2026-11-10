<script setup lang="ts">
defineProps<{ count: number, documented: number, namespace: string, properties?: string[] }>()
</script>

<template>
  <header class="api-module">
    <div class="api-module__eyebrow"><span class="api-module__dot" />CINUX BASE <span>/</span> API REFERENCE</div>
    <div class="api-module__intro">
      <div class="api-module__copy"><slot /></div>
      <div class="api-module__mark" aria-hidden="true"><span>ƒ</span><i>x → y</i></div>
    </div>
    <div class="api-module__meta">
      <span class="api-module__namespace">{{ namespace }}</span>
      <span><strong>{{ String(count).padStart(2, '0') }}</strong> 个函数</span>
      <span class="api-module__status"><span aria-hidden="true">✓</span> {{ documented }}/{{ count }} 文档完备</span>
      <span v-if="properties?.length" class="api-module__property">{{ properties.join(' · ') }}</span>
    </div>
  </header>
</template>
