// Turns the repository's own documents (content-pages.ts) into site pages: their links rewritten for the site, then
// the page's own presentation (install cards, issue cards, graphics settings).
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { createMarkdownProcessor } from '@astrojs/markdown-remark';
import { contentPages, pageForSource } from '../content-pages.ts';
import { editUrl, siteUrl, sourceUrl } from '../site.ts';
import { formatInstallHub } from './format-install.ts';
import { formatGraphicsSettings } from './graphics-settings.ts';
import { formatCommonIssues } from './format-common-issues.ts';

// Astro runs from website/; the documents are in the repository around it.
export const repoRoot = path.resolve('..');
const repoFile = (file: string) => path.join(repoRoot, file);

// Repository files the site serves itself (copied into public/ by the repoAssets integration).
const publicAssets = new Set(['assets/longshot.png']);

/** Every file a page is made from, for rebuilding the pages when one changes in dev. */
export const sourceFiles = [...new Set([...Object.values(contentPages), 'gpu/vk/vk_menu.cpp'])].map(repoFile);

function urlFor(value: string, source: string) {
  // The documents link to the site with its full address, which also works on Forgejo; on the site itself, a path.
  if (value.startsWith(siteUrl + '/')) return value.slice(siteUrl.length);
  if (!value || value.startsWith('/') || /^(?:[a-z]+:|\/\/|#)/i.test(value)) return value;
  const hashAt = value.indexOf('#');
  const fragment = hashAt < 0 ? '' : value.slice(hashAt);
  const pathname = hashAt < 0 ? value : value.slice(0, hashAt);
  const resolved = path.posix.normalize(path.posix.join(path.posix.dirname(source), pathname));
  const page = pageForSource.get(resolved);
  if (page !== undefined) return '/' + (page ? `${page}/` : '') + fragment;
  if (publicAssets.has(resolved)) return '/' + resolved + fragment;
  return sourceUrl(resolved) + fragment;
}

export interface RepoPage { title: string; markdown: string; editUrl: string; tableOfContents?: false }

export async function renderPage(slug: string): Promise<RepoPage> {
  const source = contentPages[slug];
  let text = await readFile(repoFile(source), 'utf8');
  const title = text.match(/^# (.+)$/m)?.[1] || source;
  text = text.replace(/^# .+\n/m, '');
  // Keep code samples untouched; only rewrite actual prose links and HTML attributes.
  text = text.split(/(^```[^\n]*\n[\s\S]*?^```\s*$)/gm).map((chunk) => chunk.startsWith('```') ? chunk : chunk
    .replace(/(!?\[[^\]]*\]\()([^\s)]+)(\))/g, (_, before, url, after) => before + urlFor(url, source) + after)
    .replace(/(\b(?:src|href)=")(?!https?:|#)([^"]+)(")/g, (_, before, url, after) => before + urlFor(url, source) + after),
  ).join('');
  if (source === 'docs/getting-started.md') text = formatInstallHub(text);
  if (source === 'docs/common-issues.md') text = formatCommonIssues(text);
  if (source === 'docs/graphics-and-controls.md') {
    text = await formatGraphicsSettings(text, await readFile(repoFile('README.md'), 'utf8'),
      await readFile(repoFile('gpu/vk/vk_menu.cpp'), 'utf8'), await createMarkdownProcessor());
  }
  return { title, markdown: text, editUrl: editUrl(source),
    ...(source === 'docs/common-issues.md' ? { tableOfContents: false as const } : {}) };
}
