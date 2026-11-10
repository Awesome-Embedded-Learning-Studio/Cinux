---
layout: home
title: Cinux 文档
description: Cinux OS 的系统文档、手册与更新记录

hero:
  name: "Cinux"
  text: "系统文档与接口参考"
  tagline: 从引导扇区到用户态：内核各子系统的机制说明、设计取舍与工程约定，跟随当前实现更新
  actions:
    - theme: brand
      text: 系统文档
      link: /systems/
    - theme: alt
      text: API 参考
      link: /api/
    - theme: alt
      text: 更新记录
      link: /changelog/

features:
  - icon: 📘
    title: 系统文档
    details: 启动流程、内存管理、进程与调度、文件系统、驱动、图形栈与用户态
    link: /systems/
    linkText: 进入
  - icon: ⚙️
    title: API 参考
    details: 42 个分组的接口手册：签名、参数表、引入版本与源码位置，全部由源码注释生成
    link: /api/
    linkText: 查接口
  - icon: 📐
    title: 手册
    details: 文档站怎么建、怎么查、怎么发：命令、卷的构成与 CI 流程
    link: /handbook/
    linkText: 查看
  - icon: 🧾
    title: 更新记录
    details: 版本变化与主线切换的说明
    link: /changelog/
    linkText: 查看
---

## 按目标找入口

- 想知道某个子系统怎么工作：从[系统文档](/systems/)进入。启动流程先看[Bootloader](/systems/boot)，内存子系统从[物理内存管理（PMM）](/systems/kernel/mm/pmm)看起。
- 想确认某个行为属于哪个版本：见[更新记录](/changelog/)。
- 想从头学起：走教程站 Cinux-Book。本站只讲当前实现，不重复教程内容。

## 本站与源码的关系

页面内容直接来自 `document/` 目录，与仓库同分支发布。每页页脚标出最后更新时间，右上角的编辑链接直达 GitHub 对应文件。
