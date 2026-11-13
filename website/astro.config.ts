import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import { repository, siteUrl, editUrl } from './src/site.ts';
import { repoAssets } from './src/repo-docs/assets.ts';

export default defineConfig({
  site: siteUrl,
  trailingSlash: 'always',
  integrations: [repoAssets(), starlight({
    title: 'Longshot',
    description: 'Longshot by Team MagicBean. Guides, project updates, and practical help for your PC adventure.',
    favicon: '/branding/longshot.png',
    customCss: ['./src/styles/theme.css', './src/styles/content.css'],
    components: {
      Header: './src/components/Header.astro',
      Hero: './src/components/LongshotHero.astro',
      Footer: './src/components/Footer.astro',
    },
    editLink: { baseUrl: editUrl('website/') },
    tableOfContents: { minHeadingLevel: 2, maxHeadingLevel: 2 },
    sidebar: [
      { label: 'Get started', items: [
        { label: 'Install', slug: 'install' },
        { label: 'System requirements', slug: 'requirements' },
        { label: 'Settings', slug: 'settings' },
        { label: 'Updates & saves', slug: 'saves' },
        { label: 'Transfer a save', slug: 'saves/transfer' },
      ] },
      { label: 'Help', items: [
        { label: 'Common problems', slug: 'help' },
        { label: 'Report a problem', slug: 'help/report' },
        { label: 'Antivirus warnings', slug: 'help/antivirus' },
      ] },
      { label: 'Development', items: [
        { label: 'Progress', slug: 'progress' },
        { label: 'Contributing', slug: 'contributing' },
        { label: 'Source code ↗', link: repository },
      ] },
      { label: 'About', items: [
        { label: 'About Longshot', slug: 'about' },
        { label: 'Team MagicBean', slug: 'about/team' },
      ] },
    ],
  })],
});
