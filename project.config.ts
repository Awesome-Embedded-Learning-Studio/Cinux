import { defineProject } from './site/.vitepress/config/schema'

export default defineProject({
  name: 'cinux',
  title: { 'zh-CN': 'Cinux 文档' },
  description: { 'zh-CN': 'Cinux OS 的系统文档、手册与更新记录' },
  base: '/Cinux/',
  copyright: 'Copyright © 2026 Charliechen - 保留所有权利',

  documentsDir: 'document',
  siteDir: 'site',

  locales: [
    { code: 'zh-CN', label: '中文', default: true },
  ],

  nav: {
    'zh-CN': [
      { text: '首页', link: '/' },
      { text: '系统文档', link: '/systems/' },
      { text: 'API 参考', link: '/api/' },
      { text: '手册', link: '/handbook/' },
      { text: '更新记录', link: '/changelog/' },
      {
        text: '相关',
        items: [
          { text: '教程站（Cinux-Book）', link: 'https://awesome-embedded-learning-studio.github.io/Cinux-Book/' },
          { text: '在 GitHub 上看源码', link: 'https://github.com/Awesome-Embedded-Learning-Studio/Cinux' },
        ],
      },
    ],
  },

  sidebar: {
    volumes: [
      { name: 'systems', srcDir: 'document', urlPrefix: '/systems' },
      { name: 'handbook', srcDir: 'handbook', urlPrefix: '/handbook' },
      { name: 'changelog', srcDir: 'changelogs', urlPrefix: '/changelog' },
      // 设计笔记卷(TLB shootdown 等独立成篇的设计记录)
      { name: 'notes', srcDir: 'notes', urlPrefix: '/notes' },
      // CI 分卷构建说明;单体构建的 rewrites 已把 ci/ 映射到 /guide,
      // 卷前缀与其保持一致,翻页邻居的链接才对得上
      { name: 'ci', srcDir: 'ci', urlPrefix: '/guide' },
      // api 卷是 pnpm docs:api 的生成件(document/api/),fresh clone 上
      // 未生成时 build.ts 对不存在的卷自动跳过
      { name: 'api', srcDir: 'api', urlPrefix: '/api' },
    ],
  },

  github: {
    owner: 'Awesome-Embedded-Learning-Studio',
    repo: 'Cinux',
    branch: 'main',
    documentsPath: 'document',
  },

  build: {
    concurrency: 4,
    cacheDir: '.build-cache',
    // root 层只产出首页
    rootPages: ['index.md', '404.md'],
    rootAssets: [],
  },

  plugins: {
    cppTemplateEscape: true,
    kbd: true,
    math: true,
    mermaid: true,
    codeFold: true,
  },
})
