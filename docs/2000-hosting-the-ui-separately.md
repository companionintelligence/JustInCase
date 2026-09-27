# Running the UI apart from the server

How to serve the web UI from one place and answer questions from another — a
laptop, a box on the LAN, ci-hub — and which of those the browser will actually
allow.

> **Private & Confidential — Property of Lifescope Inc. Do not distribute.**

---

## The default, which needs none of this

`jic-server` serves `public/` itself. The page and the API share an origin,
every request is a relative path, and there is nothing to configure. The
**Backend** row in the sidebar reads *this device*. If that is your deployment,
stop here.

Everything below is for splitting the two apart.

## Pointing the UI somewhere else

Open the sidebar, press **Server** next to *System*, type the server's URL:

```
http://localhost:8080          a JIC on this machine
http://192.168.1.50:8080       a JIC on the LAN
https://jic.ci.computer        a JIC behind the Cloudflare tunnel
```

It is stored in `localStorage` under `jic.backendUrl`, per browser, and
**This device** clears it. Nothing is sent anywhere: the setting only changes
which host the page's own requests go to.

## What the browser allows

This is the part that decides which topologies are real. Two separate browser
rules apply, and they are easy to confuse.

### Mixed content

A page served over **https** may not make plain-**http** requests — with one
carve-out that matters here: loopback addresses are "potentially trustworthy",
so `http://localhost` and `http://127.0.0.1` **are** allowed from an https
page. A private LAN address like `http://192.168.1.50` is **not** covered by
that carve-out on its own.

### Local Network Access

Separately, Chrome 142+ asks permission before a **public** page reaches a
**local** address — loopback, an RFC1918 address, or a `.local` name. The
visitor sees a prompt along the lines of *"Look for and connect to any device
on your local network"*, once per site.

Chrome's documentation suggests declaring `targetAddressSpace: "local"` on
such a request so the destination is known before it is resolved. **The UI
deliberately does not**, because tested against Chromium it does not behave as
documented: the request goes out with a target space of `unknown`, Chrome
compares that against the resource's actual `loopback` space, and blocks it —

```
Access to fetch at 'http://127.0.0.1:9101/status' from origin
'http://127.0.0.1:9100' has been blocked by CORS policy: Request had a target
IP address space of `unknown` yet the resource is in address space `loopback`.
```

That turns a request which works fine unannotated into a hard CORS failure.
Without the annotation, same-address-space requests are untouched and a public
page reaching a local server simply prompts for permission once, which is the
intended flow. Revisit if the option's behaviour settles.

### So, in practice

| UI served from | Server at | Works? |
|---|---|---|
| `http://localhost:8080` (JIC itself) | same | ✅ the default, nothing to configure |
| `https://…` static host | `http://localhost:8080` | ✅ after the one-time Chrome permission prompt |
| `https://…` static host | `http://192.168.1.50:8080` | ⚠️ permission prompt, and Safari/Firefox may still refuse the http hop |
| `https://…` static host | `https://jic.ci.computer` (tunnel) | ✅ cleanest remote option — no prompt, no mixed content |
| `http://` local page | any local server | ✅ no prompt (same address space) |

**The most robust split** is a static UI plus an https server behind the
Cloudflare tunnel: no mixed content, no permission prompt, no browser
differences. If the server must stay on your machine and be reached from a
public page, `http://localhost` is the reliable address — not the LAN IP.

## Server-side setup

### 1. Allow the UI's origin to call the API (`JIC_CORS_ORIGIN`)

A cross-origin request needs the server's permission. Set the origin the UI is
served from:

```bash
JIC_CORS_ORIGIN=https://jic.ci.computer docker compose up -d
```

> **Do not use `*` on a machine you care about.** With a wildcard, *any* page
> in *any* tab can query your JIC and read back passages from your documents.
> The value should name the one origin your UI is served from. This matters
> most in exactly the setup this document describes, where the server sits on
> a personal machine.

### 2. Only if JIC serves the UI (`JIC_CONNECT_SRC`)

When the page comes from a static host, that host's policy applies and there is
nothing to do. But if you open a JIC-served UI and point it at a *different*
server, the page's own `Content-Security-Policy` blocks it — `connect-src` is
`'self'` by default. Name the other server:

```bash
JIC_CONNECT_SRC="https://jic.ci.computer" docker compose up -d
```

Without it the failure is a console CSP violation rather than a connection
error, which is the most confusing way for this to break.

## Hosting the UI as static files

`public/` is dependency-free — `index.html`, `app.js`, `style.css` and
`assets/`. Any static host will serve it:

```bash
# Cloudflare Pages, from the repo root
npx wrangler pages deploy public --project-name jic-ui
```

Two things do not come with it, because they are served by the server rather
than the static host:

- **`/sources/` documents.** Citation and library links are rewritten to point
  at the configured server, so they resolve — but only while that server is
  reachable.
- **`/status`, `/api/library`, `/query`.** Until a backend is configured the
  page renders its offline shell and the status pill reads *Server
  unreachable*, which is the honest state rather than an error.

## Failure modes

| Symptom | Cause |
|---|---|
| Status pill stuck on *Server unreachable* | Wrong URL, server down, or `JIC_CORS_ORIGIN` unset |
| Console: CSP `connect-src` violation | JIC serves the page; set `JIC_CONNECT_SRC` |
| Console: blocked mixed content | https page → http server that is not loopback |
| Chrome permission prompt refused | Re-allow under the site's permissions |
| Works in Chrome, not Safari/Firefox | Local Network Access and the loopback carve-out differ between engines; prefer the https tunnel |

## Why the setting lives in the browser

It is per-person and per-machine: two people using one static deployment point
at different servers, and a rebuild to change an address would make that
impossible. It is also why it is not a build-time constant — the same UI build
serves a laptop, a LAN box and a tunnel.
