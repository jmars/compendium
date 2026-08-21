// shell/shell.js — @mfe/framework thin-shell entry for the compendium MFE site.
//
// Boots the compendium docs app with 6 routes:
//   '/'                       → template 'fixpoint'          (cross-nav home, main site)
//   '/compendium'             → template 'compendium-landing'
//   '/compendium/config'      → template 'compendium-config'
//   '/compendium/cli'         → template 'compendium-cli'
//   '/compendium/api'         → template 'compendium-api'
//   '/compendium/playground'  → template 'compendium-playground'
//
// Matching the main site means a data-mfe-route like '/compendium' or '/'
// resolves the same way on either page, so cross-site MFE nav links agree.
//
// The pages ship statically pre-rendered (see scripts/ssg.mjs): the #app root
// carries an `ssr` attribute, so createApp rehydrates the existing DOM in
// place instead of wiping it and re-fetching the template on first paint.
//
// Rehydrate only when the current pathname (trailing-slash-stripped) matches
// a pre-rendered compendium page (all of them, including playground — the Elm
// app renders hero/sections and the <compendium-playground> element boots
// client-side).

import { createApp } from '@mfe/framework';

const app = await createApp({
  root: document.getElementById('app'),
  routes: [
    { path: '/', template: 'fixpoint', name: 'home' },
    { path: '/compendium', template: 'compendium-landing', name: 'compendium-landing' },
    { path: '/compendium/config', template: 'compendium-config', name: 'compendium-config' },
    { path: '/compendium/cli', template: 'compendium-cli', name: 'compendium-cli' },
    { path: '/compendium/api', template: 'compendium-api', name: 'compendium-api' },
    { path: '/compendium/playground', template: 'compendium-playground', name: 'compendium-playground' },
  ],
  basePath: '/',
  // compendium's templates are served from /compendium/shell/templates
  // (the main site owns /shell/templates). Pin the baseURL here so both route
  // templates resolve under this site's shell regardless of the deep-link subpath.
  baseURL: '/compendium/shell/templates',
  // The SSG output pre-renders all content pages EXCEPT the playground. The
  // playground page ships client-booted: the <compendium-playground> custom
  // element builds its editor on connectedCallback, and SSR rehydration would
  // create a SECOND element (double editor). Rehydrate only the pre-rendered
  // non-playground routes; the playground route gets a fresh client render
  // instead.
  ssr: (() => {
    const path = (window.location.pathname.replace(/\/+$/, '') || '/');
    return (path === '/compendium' || path.startsWith('/compendium/'))
      && path !== '/compendium/playground';
  })(),
});

// Expose the app handle so the shell/host can inspect or drive it later.
window.__compendiumApp = app;
