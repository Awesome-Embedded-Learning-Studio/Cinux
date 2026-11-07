---
layout: home
title: Cinux 文档
description: Cinux OS 的系统文档、设计说明、开发指南与更新记录

hero:
  name: "Cinux"
  text: "系统文档与接口参考"
  tagline: 从引导扇区到用户态：内核各子系统的机制说明、设计取舍与工程约定，跟随当前实现更新
  actions:
    - theme: brand
      text: 系统文档
      link: /systems/
    - theme: alt
      text: 开发指南
      link: /guide/
    - theme: alt
      text: 更新记录
      link: /changelog/

features:
  - title: 系统文档
    details: 启动流程、内存管理、进程与调度、文件系统、驱动、图形栈与用户态
    link: /systems/
    linkText: 进入
  - title: 设计文档
    details: Cinux-Base 基础库的设计规范：header-only、constexpr、无堆分配
    link: /design/
    linkText: 查看
  - title: 开发指南
    details: 分支策略、提交规范、PR 流程、CI 流水线与本地复现
    link: /guide/
    linkText: 查看
  - title: 更新记录
    details: 版本变化与主线切换的说明
    link: /changelog/
    linkText: 查看
---

## 按目标找入口

- 想知道某个子系统怎么工作：从[系统文档](/systems/)进入。启动流程先看[Bootloader](/systems/boot)，内存子系统从[物理内存管理（PMM）](/systems/kernel/mm/pmm)看起。
- 想查工程约定、提交流程与 CI：见[开发指南](/guide/)。
- 想确认某个行为属于哪个版本：见[更新记录](/changelog/)。
- 想从头学起：走教程站 Cinux-Book。本站只讲当前实现，不重复教程内容。

## 本站与源码的关系

页面内容直接来自 `document/` 目录，与仓库同分支发布。每页页脚标出最后更新时间，右上角的编辑链接直达 GitHub 对应文件。
