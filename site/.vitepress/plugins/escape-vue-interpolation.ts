// Vue 插值转义:把 markdown 行内文本里的 {{ 与 }} 转成 HTML 实体。
//
// 背景:开发日志里有 C++ 花括号初始化(EthAddr{{...}}),VitePress 把
// md 渲染产物当 Vue 模板编译,{{...}} 会被当成 JS 表达式直接构建失败。
// 转成实体后 Vue 编译器不认,浏览器渲染回花括号,显示不受影响。
//
// 只处理行内 text 与 code_inline:fence 代码块由 VitePress 的高亮器
// 输出 v-pre 包裹,天然不受插值影响。text token 的 content 在渲染时
// 会被 escapeHtml,实体放 content 里会二次转义,所以替换成 html_inline
// (原样输出);code_inline 同理,但外层补回 <code> 标签保持样式。

import type { PluginSimple } from 'markdown-it'
import type MarkdownIt from 'markdown-it'

function escapeInterp(text: string): string {
  return text
    .replace(/&/g, '&amp;')   // html_inline 不走 escapeHtml,自转 & 防注入
    .replace(/</g, '&lt;')
    .replace(/\{\{/g, '&#123;&#123;')
    .replace(/\}\}/g, '&#125;&#125;')
}

export const escapeVueInterpolation: PluginSimple = (md: MarkdownIt) => {
  md.core.ruler.after('inline', 'escape_vue_interpolation', (state) => {
    for (const token of state.tokens) {
      if (!token.children) continue
      for (let i = 0; i < token.children.length; i++) {
        const child = token.children[i]
        if (child.type === 'text' && child.content.includes('{{')) {
          child.type = 'html_inline'
          child.content = escapeInterp(child.content)
        } else if (child.type === 'code_inline' && child.content.includes('{{')) {
          child.type = 'html_inline'
          child.content = `<code>${escapeInterp(child.content)}</code>`
        }
      }
    }
    return true
  })
}
