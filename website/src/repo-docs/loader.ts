// Starlight's docs collection: the pages in src/content/docs, plus the repository's own documents (content-pages.ts)
// rendered straight from their files. Nothing is written to disk; in dev, a change to a source file rebuilds them.
import type { Loader, LoaderContext } from 'astro/loaders';
import { docsLoader } from '@astrojs/starlight/loaders';
import { contentPages } from '../content-pages.ts';
import { renderPage, sourceFiles } from './pages.ts';

async function loadRepoPages(context: LoaderContext) {
  for (const slug of Object.keys(contentPages)) {
    const { markdown, ...frontmatter } = await renderPage(slug);
    const data = await context.parseData({ id: slug, data: frontmatter });
    context.store.set({ id: slug, data, body: markdown, rendered: await context.renderMarkdown(markdown),
      digest: context.generateDigest(markdown) });
  }
}

export function longshotDocsLoader(): Loader {
  const docs = docsLoader();
  return {
    name: 'longshot-docs',
    async load(context) {
      await docs.load(context);
      await loadRepoPages(context);
      if (!context.watcher) return;
      context.watcher.add(sourceFiles);
      context.watcher.on('change', async (file) => {
        if (sourceFiles.includes(file)) await loadRepoPages(context);
      });
    },
  };
}
