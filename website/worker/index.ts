// longshot.magicbean.dev's Worker: the built site (dist/), and permanent download links for the Longshot Launcher.
//
//   /download                                   a page: the launcher for this computer, from the best channel there is
//   /download/launcher/<os>                     302 to that file of the best channel (stable, else rc, else nightly)
//   /download/launcher/<channel>                302 to that channel's newest release page
//   /download/launcher/<channel>/<os>           302 to that file of the channel's newest release
//
// <channel> is stable, rc or nightly; <os> is windows (the setup), windows-portable (the zip) or linux (the AppImage).
// The channels follow the launcher's rules (Core/ReleaseManifest.cs, Newest; scripts/release.py, newest): drafts never;
// stable the newest vX.Y.Z that is not a pre-release; rc the newest vX.Y.Z-rc.N or vX.Y.Z; nightly the newest
// nightly-<count>-<8 hex> by its count. The release list is read from git.magicbean.dev and kept 60 seconds.

const FORGE = "https://git.magicbean.dev";
const REPO = "MagicBeanDev/LongshotLauncher";
const FIRST_MAJOR = 1;
const CACHE_SECONDS = 60;
const CHANNELS = ["stable", "rc", "nightly"];
const FILES = {
  windows: { suffix: "-windows-x64-setup.exe", label: "Windows installer", note: "setup.exe" },
  "windows-portable": { suffix: "-windows-x64-portable.zip", label: "Windows portable", note: "zip, no install" },
  linux: { suffix: "-x86_64.AppImage", label: "Linux", note: "AppImage" },
};
const CHANNEL_NAME = { stable: "Stable", rc: "Release candidate", nightly: "Nightly" };
const CHANNEL_NOTE = {
  stable: "for playing",
  rc: "the next version, to try before it is stable",
  nightly: "every change, untested",
};

const STABLE = /^v(\d+)\.(\d+)\.(\d+)$/;
const VERSION = /^v(\d+)\.(\d+)\.(\d+)(?:-rc\.(\d+))?$/;
const NIGHTLY = /^nightly-([0-9]{1,9})-([0-9a-f]{8})$/;

export function versionKey(tag) {
  const m = VERSION.exec(tag || "");
  if (!m) return null;
  return [+m[1], +m[2], +m[3], m[4] ? 0 : 1, +(m[4] || 0)];
}

function compareKeys(a, b) {
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return a[i] - b[i];
  return 0;
}

/** The release a channel offers, from Forgejo's release list, or null */
export function newest(releases, channel) {
  const published = releases.filter((r) => !r.draft);
  if (channel === "nightly") {
    let best = null;
    for (const r of published) {
      const m = NIGHTLY.exec(r.tag_name);
      if (m && (!best || +m[1] > +NIGHTLY.exec(best.tag_name)[1])) best = r;
    }
    return best;
  }
  let best = null;
  for (const r of published) {
    const k = versionKey(r.tag_name);
    if (!k || k[0] < FIRST_MAJOR) continue;
    if (channel === "stable" && (!STABLE.test(r.tag_name) || r.prerelease)) continue;
    if (!best || compareKeys(k, versionKey(best.tag_name)) > 0) best = r;
  }
  return best;
}

/** The release asset of a kind (windows, windows-portable, linux), or null */
export function assetOf(release, kind) {
  const suffix = FILES[kind].suffix;
  return (release.assets || []).find((a) => a.name.startsWith("LongshotLauncher-") && a.name.endsWith(suffix)) || null;
}

/** The best channel there is: stable, else rc, else nightly; [channel, release] or [null, null] */
export function best(releases) {
  for (const c of CHANNELS) {
    const r = newest(releases, c);
    if (r) return [c, r];
  }
  return [null, null];
}

// ---- the release list, cached ----

async function fetchReleases(ctx) {
  const cache = caches.default;
  const key = new Request(`${FORGE}/api/v1/repos/${REPO}/releases?magicbean-site-cache=1`);
  const hit = await cache.match(key);
  if (hit) return hit.json();
  const all = [];
  for (let page = 1; page <= 5; page++) {
    const r = await fetch(`${FORGE}/api/v1/repos/${REPO}/releases?limit=50&page=${page}`, {
      // (Cloudflare refuses requests with no browser or tool User-Agent)
      headers: { "User-Agent": "longshot-website (+https://longshot.magicbean.dev)", Accept: "application/json" },
    });
    if (!r.ok) throw new Error(`git.magicbean.dev answered ${r.status}`);
    const items = await r.json();
    if (!Array.isArray(items)) throw new Error("git.magicbean.dev answered no release list");
    all.push(...items);
    if (items.length < 50) break;
  }
  const slim = all.map((r) => ({
    tag_name: r.tag_name, draft: !!r.draft, prerelease: !!r.prerelease, html_url: r.html_url, published_at: r.published_at,
    assets: (r.assets || []).map((a) => ({ name: a.name, size: a.size, browser_download_url: viaCdn(a.browser_download_url) })),
  }));
  const stored = new Response(JSON.stringify(slim), {
    headers: { "Content-Type": "application/json", "Cache-Control": `public, max-age=${CACHE_SECONDS}` },
  });
  ctx.waitUntil(cache.put(key, stored));
  return slim;
}

// ---- pages ----

const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
const mb = (n) => (n ? `${Math.round(n / 1048576)} MB` : "");

function page(title, body, status = 200) {
  return new Response(`<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>${esc(title)}</title>
  <link rel="icon" type="image/png" sizes="32x32" href="/favicon-32.png">
  <meta name="theme-color" content="#1a0601">
  <style>
    :root {
      --bg: #1a0601; --fg: #fbe9c8; --muted: #e6c9a0; --line: rgba(251, 233, 200, .16); --pill: rgba(251, 233, 200, .06);
      --green: #7cc243; --green-ink: #142705; --gold: #f6c453;
      color-scheme: dark;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0; min-height: 100vh; display: grid; place-items: center; padding: 32px 16px;
      background: var(--bg); color: var(--fg); line-height: 1.5;
      font-family: ui-rounded, "SF Pro Rounded", "Nunito", "Varela Round", ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif;
    }
    main { width: 100%; max-width: 640px; text-align: center; }
    .logo { width: 120px; height: auto; }
    h1 { font-size: clamp(1.8rem, 7vw, 2.6rem); font-weight: 800; line-height: 1.1; margin: .25rem 0 .5rem; }
    p { color: var(--muted); margin: .5rem 0; }
    a { color: var(--gold); }
    .big {
      display: inline-flex; flex-direction: column; align-items: center; margin: 1rem 0 .25rem; padding: 1rem 2rem;
      border-radius: 18px; background: var(--green); color: var(--green-ink); text-decoration: none; font-weight: 800;
      font-size: 1.25rem; box-shadow: 0 3px 0 rgba(0, 0, 0, .35);
    }
    .big small { font-weight: 600; font-size: .85rem; opacity: .8; }
    .big:hover { filter: brightness(1.08); }
    .big:focus-visible, .pill:focus-visible { outline: 2px solid var(--gold); outline-offset: 3px; }
    .others { display: flex; flex-wrap: wrap; justify-content: center; gap: .5rem; margin: .5rem 0 1.5rem; }
    .pill {
      display: inline-block; padding: .45rem 1rem; border-radius: 999px; border: 1px solid var(--line); background: var(--pill);
      color: var(--muted); font-weight: 600; text-decoration: none;
    }
    .pill:hover { background: var(--green); border-color: var(--green); color: var(--green-ink); }
    .channels { margin-top: 1.25rem; }
    .channel { border-top: 1px solid var(--line); padding: .75rem 0 .25rem; }
    .channel h3 { margin: 0 0 .25rem; font-size: 1.15rem; }
    .channel h3 small { font-weight: 600; color: var(--muted); font-size: .85rem; }
    .channel .others { margin: .5rem 0; }
    .warn { border: 1px solid var(--gold); border-radius: 12px; padding: .75rem 1rem; margin: 1rem 0; color: var(--fg); }
    code { font-size: .9em; }
  </style>
</head>
<body>
  <main>
    <img class="logo" src="/logo.webp" alt="" width="512" height="512">
${body}
  </main>
</body>
</html>
`, {
    status,
    headers: { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store", Vary: "User-Agent" },
  });
}

function osOf(request) {
  const ua = request.headers.get("User-Agent") || "";
  if (/Windows/i.test(ua)) return "windows";
  if (/Android|iPhone|iPad|CrOS/i.test(ua)) return null;
  if (/Linux|X11/i.test(ua)) return "linux";
  return null;
}

function channelTable(releases) {
  const rows = CHANNELS.map((c) => {
    const r = newest(releases, c);
    const head = `<h3>${CHANNEL_NAME[c]} <small>${esc(CHANNEL_NOTE[c])}</small></h3>`;
    if (!r) return `<div class="channel">${head}<p>None yet.</p></div>`;
    const links = Object.keys(FILES).filter((k) => assetOf(r, k))
      .map((k) => `<a class="pill" href="/download/launcher/${c}/${k}">${esc(FILES[k].label)}</a>`).join("");
    return `<div class="channel">${head}<div class="others">${links}</div>` +
      `<p><a href="/download/launcher/${c}">${esc(r.tag_name)}</a>, its release page</p></div>`;
  });
  return `<div class="channels">${rows.join("")}</div>`;
}

const NOT_THE_BUILDER = `<p>Download the <strong>launcher</strong>: it installs the builder and builds the game from your own copy.
    The <em>LongshotBuilder</em> files on Longshot's releases are for the launcher, not for you to run.</p>`;

function downloadPage(request, releases) {
  const [channel, r] = best(releases);
  if (!r) {
    return page("Download the Longshot Launcher", `    <h1>Longshot Launcher</h1>
    <p>There is no release yet. Watch <a href="${FORGE}/${REPO}/releases">the release page</a>.</p>`, 404);
  }
  const os = osOf(request);
  const primary = os && assetOf(r, os) ? os : null;
  const bigs = (primary ? [primary] : ["windows", "linux"]).filter((k) => assetOf(r, k)).map((k) => {
    const a = assetOf(r, k);
    return `<a class="big" href="/download/launcher/${k}">Download for ${k === "windows" ? "Windows" : "Linux"}` +
      `<small>${esc(FILES[k].note)} · ${esc(mb(a.size))} · ${esc(r.tag_name)}</small></a>`;
  }).join(" ");
  const others = Object.keys(FILES).filter((k) => k !== primary && assetOf(r, k))
    .map((k) => `<a class="pill" href="/download/launcher/${k}">${esc(FILES[k].label)} (${esc(FILES[k].note)})</a>`).join("");
  const warn = channel === "stable" ? "" :
    `<div class="warn">There is no stable launcher yet: this is the newest <strong>${channel}</strong> (${esc(CHANNEL_NOTE[channel])}).</div>`;
  return page("Download the Longshot Launcher", `    <h1>Longshot Launcher</h1>
    ${NOT_THE_BUILDER}
    ${warn}
    ${bigs}
    <div class="others">${others}</div>
    <h2>Every channel</h2>
    ${channelTable(releases)}
    <p>How to install and play: <a href="${FORGE}/MagicBeanDev/Longshot#install-and-play">Longshot's README</a>.
    All releases: <a href="${FORGE}/${REPO}/releases">git.magicbean.dev</a>.</p>`);
}

function notFound(releases, what, kind) {
  const [channel, r] = best(releases);
  let offer = r ? `<p>The newest launcher there is: <strong>${esc(r.tag_name)}</strong> (${channel}).</p>` : "";
  if (r && kind && FILES[kind] && assetOf(r, kind)) {
    offer += `<a class="big" href="/download/launcher/${channel}/${kind}">Get ${esc(r.tag_name)} instead` +
      `<small>${esc(FILES[kind].label)} · ${esc(FILES[kind].note)} · ${esc(CHANNEL_NOTE[channel])}</small></a>`;
  }
  return page("Not found: Longshot Launcher", `    <h1>${esc(what)}</h1>
    ${offer}
    ${channelTable(releases)}
    <p><a class="pill" href="/download">Back to the download page</a></p>`, 404);
}

// Release files are served by git-cdn.magicbean.dev, from Cloudflare's edge: the same path on its own host.
function viaCdn(url) {
  return typeof url === "string" ? url.replace(/^https:\/\/git\.magicbean\.dev\//, "https://git-cdn.magicbean.dev/") : url;
}

function redirect(url) {
  return new Response(null, { status: 302, headers: { Location: url, "Cache-Control": `public, max-age=${CACHE_SECONDS}` } });
}

async function download(request, ctx, parts) {
  let releases;
  try {
    releases = await fetchReleases(ctx);
  } catch (e) {
    return page("Longshot Launcher", `    <h1>Can't reach git.magicbean.dev</h1>
    <p>${esc(e.message)}. Try again in a minute, or download from <a href="${FORGE}/${REPO}/releases">the release page</a>.</p>`, 502);
  }
  // /download
  if (parts.length === 0) return downloadPage(request, releases);
  if (parts[0] !== "launcher") return notFound(releases, "No such download");
  // /download/launcher
  if (parts.length === 1) return redirect("/download");
  let [channel, kind] = parts.slice(1);
  if (parts.length === 2 && FILES[channel]) {
    // /download/launcher/<os>: the best channel there is
    [kind, channel] = [channel, best(releases)[0]];
    if (!channel) return notFound(releases, "No launcher release yet");
  }
  if (!CHANNELS.includes(channel) || parts.length > 3 || (kind !== undefined && !FILES[kind])) {
    return notFound(releases, "No such download");
  }
  const r = newest(releases, channel);
  if (!r) return notFound(releases, `No ${channel} launcher yet`, kind);
  if (kind === undefined) return redirect(r.html_url);
  const a = assetOf(r, kind);
  if (!a) return notFound(releases, `${r.tag_name} has no ${FILES[kind].label} file`, kind);
  return redirect(a.browser_download_url);
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/+$/, "");
    if (path === "/download" || path.startsWith("/download/")) {
      if (request.method !== "GET" && request.method !== "HEAD") return new Response("Method not allowed", { status: 405, headers: { Allow: "GET, HEAD" } });
      const parts = path.split("/").slice(2).filter(Boolean).map((p) => p.toLowerCase());
      return download(request, ctx, parts);
    }
    return env.ASSETS.fetch(request);
  },
};
