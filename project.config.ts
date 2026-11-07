import { defineProject } from './site/.vitepress/config/schema'

export default defineProject({
  name: 'cinux',
  title: { 'zh-CN': 'Cinux 文档' },
  description: { 'zh-CN': 'Cinux OS 的系统文档、设计说明、开发指南与更新记录' },
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
      { text: '设计', link: '/design/' },
      { text: '开发指南', link: '/guide/' },
      { text: '更新记录', link: '/changelog/' },
      { text: '开发日志', link: '/notes/' },
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
      { name: 'design', srcDir: 'design', urlPrefix: '/design' },
      { name: 'guide', srcDir: 'ci', urlPrefix: '/guide' },
      { name: 'notes', srcDir: 'notes', urlPrefix: '/notes' },
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
