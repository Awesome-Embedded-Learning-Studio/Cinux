import DefaultTheme from 'vitepress/theme'
import { defineAsyncComponent, h } from 'vue'
import type { Theme } from 'vitepress'
import './custom.css'
import './article-doc.css'
import './article-code.css'
import './article-quote.css'
import './article-figure.css'
import './article-nav.css'

// 阅读体验
import FontSizeSwitcher from './components/FontSizeSwitcher.vue'
import NavSpinner from './components/NavSpinner.vue'
import ReadingProgress from './components/ReadingProgress.vue'
import ResizableSidebar from './components/ResizableSidebar.vue'
import MediaLightbox from './components/MediaLightbox.vue'
// 首页
import HomeHeroVisual from './components/HomeHeroVisual.vue'
import HomeShowcase from './components/HomeShowcase.vue'
import HomeFeatureGrid from './components/HomeFeatureGrid.vue'
// 文档页
import DocNavCards from './components/DocNavCards.vue'
import ApiGroupExplorer from './components/ApiGroupExplorer.vue'

import { setupMermaid } from './mermaid-client'
import { setupFigureZoom } from './figure-zoom'

export default {
  extends: DefaultTheme,
  Layout() {
    return h(DefaultTheme.Layout, null, {
      // 全局:路由切换 spinner + 阅读进度条 + 可拖拽侧栏 + 图片/流程图灯箱
      'layout-top': () => [
        h(NavSpinner),
        h(ReadingProgress),
        h(ResizableSidebar),
        h(MediaLightbox),
      ],
      // 上下页导航之后:分区导航卡片
      'doc-after': () => h(DocNavCards),
      // 首页 hero 右侧视觉
      'home-hero-image': () => h(HomeHeroVisual),
      // 首页实拍区:一张真截图加三句现场说明,接在入口区之后
      'home-features-after': () => h(HomeShowcase),
      // 字号切换器:桌面顶栏 + 移动端抽屉各一份
      'nav-bar-content-after': () => h(FontSizeSwitcher),
      'nav-screen-content-after': () => h(FontSizeSwitcher),
    })
  },
  setup() {
    // mermaid 图在客户端懒加载渲染,切路由后重渲新图;灯箱/路由钩子在组件内部自注册
    setupMermaid()
    // 插图挂放大按钮(流程图的按钮由 mermaid-client 渲染完后挂,共用同一实现)
    setupFigureZoom()
  },
  enhanceApp({ app }) {
    // md 正文中可全局使用的组件(escape-cpp-templates 的 VUE_COMPONENTS
    // 白名单与之保持同步)
    app.component('HomeFeatureGrid', HomeFeatureGrid)
    app.component('ApiGroupExplorer', ApiGroupExplorer)
    app.component('ReferenceCard', defineAsyncComponent(() => import('./components/ReferenceCard.vue')))
    app.component('ReferenceItem', defineAsyncComponent(() => import('./components/ReferenceItem.vue')))
    app.component('RefLink', defineAsyncComponent(() => import('./components/RefLink.vue')))
  },
} satisfies Theme
