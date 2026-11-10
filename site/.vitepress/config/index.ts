import { defineConfig } from 'vitepress'
import { fileURLToPath } from 'url'
import config from '../../../project.config'
import { getBuildInfo } from './build-info'
import { buildSidebar } from './sidebar'
import { buildLocales } from './locales'
import {
  sharedMarkdown,
  localSearchBoxAlias,
  localSearchOptions,
  applySharedPageData,
  withSharedDrawio,
  FONT_SIZE_SCRIPT,
  SIDEBAR_WIDTH_SCRIPT,
} from './shared'

const buildInfo = getBuildInfo()
const primaryCode = config.locales.find(l => l.default)?.code ?? config.locales[0]?.code ?? 'zh'

// dev 与单体构建直接跑在仓库根的 document/ 上。
const DOCS_ROOT = fileURLToPath(new URL('../../../document', import.meta.url))

export default withSharedDrawio(defineConfig({
  base: config.base,
  cleanUrls: true,
  lastUpdated: true,

  // dev/单体构建跑在真实 document/ 上,死链当错误暴露(分卷构建放宽)。
  // 豁免只留给「源码路径」这一类:源码不在 document/ 树内,产物里天然无目标。
  // 源码路径豁免:源码不在 document/ 树内,产物里天然无目标
  // notes 卷豁免:其相对链接在渲染层由 source-link-plugin 改写/剥壳,
  // 死链检查看到的原始 href 不代表线上行为;单体构建仅作 preview。
  ignoreDeadLinks: [
    /\.(cpp|hpp|h|c|cc|asm|S|ld|sh|txt|cmake|yml|yaml|py|json)(\?|#|$)/i,
    // notes 卷的「术语(注释)」伪链接(base_[4](0x10/4) 这类),渲染层已剥壳
    /^\.\//,
  ],

  srcDir: '../document',
  outDir: '../dist-single',

  // 单体构建只渲染会上站的几卷;曾经的内部目录(todo/ai/ai_prompts)与
  // 开发日志卷已从仓库删除,这里不再需要排除项。
  srcExclude: [],

  // 目录名与线上 URL 前缀不同(卷名见 project.config)。单体构建靠 rewrites
  // 把源码路径映射到与分卷构建一致的 URL,dev 与线上地址因此对得上。
  rewrites: {
    'document/:path*': 'systems/:path*',
    'ci/:path*': 'guide/:path*',
    'changelogs/:path*': 'changelog/:path*',
  },

  title: config.title[primaryCode] ?? config.name,
  description: config.description[primaryCode] ?? '',

  locales: buildLocales(config),

  vue: {
    template: {
      compilerOptions: {
        // mermaid/katex 的自定义元素
        isCustomElement: (tag) => tag.includes('-') || tag.includes('.'),
      },
    },
  },

  head: [
    ['link', { rel: 'icon', href: config.favicon || `${config.base}favicon.svg` }],
    ['meta', { name: 'theme-color', content: '#F5FAF8' }],
    ['meta', { name: 'theme-color', content: '#0B1512', media: '(prefers-color-scheme: dark)' }],
    ['script', {}, FONT_SIZE_SCRIPT],
    ['script', {}, SIDEBAR_WIDTH_SCRIPT],
  ],

  markdown: sharedMarkdown,

  async transformPageData(pageData) {
    await applySharedPageData(pageData)
  },

  vite: {
    resolve: {
      alias: localSearchBoxAlias,
    },
    build: {
      chunkSizeWarningLimit: 5000,
    },
    ssr: {
      external: ['mermaid'],
    },
  },

  themeConfig: {
    nav: config.nav[primaryCode] || [],

    sidebar: buildSidebar(DOCS_ROOT, config),

    // 关闭默认主题底部 prev/next 文字导航(usePrevNext 不读 docFooter 开关,
    // 这样写才关得掉)。卡片版 DocNavCards 经 doc-after 插槽注入。
    docFooter: { prev: false, next: false },

    search: {
      provider: 'local',
      options: localSearchOptions,
    },

    editLink: {
      pattern: `https://github.com/${config.github.owner}/${config.github.repo}/edit/${config.github.branch}/${config.documentsPath}/:path`,
      text: '在 GitHub 上编辑此页',
    },

    footer: {
      message: `${buildInfo.version} · ${buildInfo.sha} · ${buildInfo.date}`,
      copyright: config.copyright,
    },

    socialLinks: [
      { icon: 'github', link: `https://github.com/${config.github.owner}/${config.github.repo}` },
    ],

    lastUpdated: {
      text: '最后更新',
    },
  },
}))
