// shell/pages.js — canonical page definitions for the compendium MFE site.
//
// Single source of truth for all routes, templates, slots, and output paths.
// Imported by both shell/shell.js (browser ESM) and scripts/ssg.mjs (Node ESM).
//
// CANONICAL ROUTE TABLE (5 compendium pages):
//   '/compendium'            → template 'compendium-landing'
//   '/compendium/config'     → template 'compendium-config'
//   '/compendium/cli'        → template 'compendium-cli'
//   '/compendium/api'        → template 'compendium-api'
//   '/compendium/playground' → template 'compendium-playground'
//
// SLOT NAME == TEMPLATE NAME for all compendium pages.
// The landing page's `dir` is '' so its output is dist/index.html.
// All other content pages have dir == slug, output to dist/<slug>/index.html.
// The cross-nav home route '/' → 'fixpoint' is handled separately (the main
// site owns /shell/templates/fixpoint.html and the importmap key
// 'fixpoint-landing').

export const PAGES = [
  {
    slug: 'compendium',
    path: '/compendium',
    slot: 'compendium-landing',
    template: 'compendium-landing',
    dir: '',
    title: 'compendium — a small, self-contained authoritative DNS server (UDP, RFC 1035), configured in Dhall, compiled to a portable APE and WebAssembly',
    type: 'content',
  },
  {
    slug: 'config',
    path: '/compendium/config',
    slot: 'compendium-config',
    template: 'compendium-config',
    dir: 'config',
    title: 'Config — compendium',
    type: 'content',
  },
  {
    slug: 'cli',
    path: '/compendium/cli',
    slot: 'compendium-cli',
    template: 'compendium-cli',
    dir: 'cli',
    title: 'CLI — compendium',
    type: 'content',
  },
  {
    slug: 'api',
    path: '/compendium/api',
    slot: 'compendium-api',
    template: 'compendium-api',
    dir: 'api',
    title: 'C API — compendium',
    type: 'content',
  },
  {
    slug: 'playground',
    path: '/compendium/playground',
    slot: 'compendium-playground',
    template: 'compendium-playground',
    dir: 'playground',
    title: 'Playground — compendium',
    type: 'content',
  },
];

// All content pages (Elm-rendered, including the playground — Elm renders the
// hero/sections; the <compendium-playground> custom element is empty in static
// HTML and boots client-side).
export const CONTENT_PAGES = PAGES;

// Just the compendium pages (all of them).
export const COMPENDIUM_PAGES = PAGES;
