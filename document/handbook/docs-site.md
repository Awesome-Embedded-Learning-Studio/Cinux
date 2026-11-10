# Docs Site Maintenance / 文档站维护手册
> 本册是 2026-05-25 从旧仓库迁移时的快照，内容未随内核演进更新，重写已排期。

文档站随仓库同源发布:内容在 `document/`,站点工具链在 `site/`,数据管线在 `site/api-gen/`。本页记录日常维护命令与各环节的职责。

## 日常命令

| 命令 | 作用 |
|---|---|
| `pnpm install` | 安装站点依赖(node 22、pnpm 10) |
| `pnpm docs:api` | 重新生成 API 数据:源码注释 → XML → 组页与索引 |
| `pnpm dev` | 本地开发服务器(改 md 即时热更) |
| `pnpm build` | 分卷并发构建,产物在 `site/.vitepress/dist` |
| `pnpm build:single` | 单体构建(构建校验与本地预览用) |
| `pnpm preview` | 预览(先同步单体构建再起服务) |
| `pnpm check:links` | 站内链接门禁 |
| `pnpm test:api` | api-gen 转换器测试 |

本地起预览时注意 WSL 场景加 `--host`:`vitepress preview site --host 0.0.0.0`。

## 各环节职责

- **内容**: `document/` 下的 markdown 是唯一内容源。系统文档卷(`document/document/`)按子系统分目录;卷的 URL 前缀(`systems`、`guide`、`changelog`)与目录名的映射在 `project.config.ts`。
- **API 数据**: 源码 Doxygen 注释 → `site/api-gen/Doxyfile` 产 XML → `site/api-gen/src` 四层转换(parse/transform/emit)→ `document/api/` 组页与 JSON bundle。`@ingroup` 的组名与中文标题维护在 `site/api-gen/src/generate-groups.ts`。
- **链接门禁**: 分卷构建不做断链检查,`pnpm check:links` 是唯一门禁,CI 里跑在构建之前。

## CI 与发布

- `ci.yml` 的 `docs` job:api-gen 测试 → API 数据生成(runner 有 doxygen 才跑)→ 链接检查 → 分卷构建。
- `deploy-docs.yml`:main 分支上文档相关路径(含源码目录,因为注释是 API 数据源)变更时构建并发布 GitHub Pages。首次启用需在仓库 Settings → Pages 选 "GitHub Actions"。

## 常见维护场景

**改了内核注释,API 页面要更新**: 推到 main 即可,部署工作流监听源码目录。本地验证跑 `pnpm docs:api && pnpm build`。

**新增 API 分组**: 在注释里用 `@ingroup <组名>`,再到 `generate-groups.ts` 的 `GROUP_TITLES` 补中文名。

**调整栏目或导航**: `project.config.ts` 的 `nav` 与 `sidebar.volumes`;新卷要在 `check-links.ts` 的 `PUBLISHED_VOLUME_DIRS` 与 `source-link-plugin.ts` 的 `PUBLISHED_DIRS` 同步登记。
