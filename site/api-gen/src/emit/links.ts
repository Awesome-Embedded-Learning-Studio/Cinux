import type { ApiSymbol } from '../model/symbol'

export interface ApiRepo { owner: string, repo: string, branch: string }

export function anchorOf(s: ApiSymbol): string {
  const base = s.qualifiedName.toLowerCase().replace(/[^a-z0-9]+/g, '-')
  return `${base}-${s.id.slice(-6)}`
}

export function githubLink(s: ApiSymbol, repo: ApiRepo): string {
  if (!s.location.file) return ''
  return `https://github.com/${repo.owner}/${repo.repo}/blob/${repo.branch}/${s.location.file}#L${s.location.line}`
}
