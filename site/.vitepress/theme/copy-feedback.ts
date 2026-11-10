import { onBeforeUnmount, ref } from 'vue'

export function useCopyFeedback() {
  const feedback = ref('')
  let timeout: ReturnType<typeof setTimeout> | undefined
  async function copy(text: string) {
    clearTimeout(timeout)
    try {
      await navigator.clipboard.writeText(text)
      feedback.value = '已复制'
    } catch {
      feedback.value = '请手动复制'
    }
    timeout = setTimeout(() => { feedback.value = '' }, 2000)
  }
  onBeforeUnmount(() => clearTimeout(timeout))
  return { feedback, copy }
}
