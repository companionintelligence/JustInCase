// ══════════════════════════════════════════════════════════════════════
// jic-gateway — the edge in front of jic.ci.computer.
//
// Cloudflare Access has already run by the time this Worker executes (Access
// sits earlier in the request pipeline), so every request that arrives here
// carries a signed assertion of a VERIFIED email address. That is the
// "email collection" half of the deployment: a visitor cannot reach the app
// without proving they can read mail at the address they typed.
//
// This Worker adds the half Access cannot do: a monthly ceiling on questions.
// It matters because the thing behind the tunnel is not elastic cloud
// compute — it is one box doing CPU inference. An unmetered /query endpoint
// is an open invitation to spend someone else's hardware.
//
//   GET  /            free — UI, assets
//   GET  /status      free — cheap
//   GET  /api/library free — cheap
//   POST /query       METERED — this is the one that runs the model
//
// Everything else is proxied untouched to the tunnel origin.
// ══════════════════════════════════════════════════════════════════════

import { createRemoteJWKSet, jwtVerify } from "jose";
import type { JWTPayload } from "jose";
import { QuotaCounter } from "./quota";
import type { QuotaReply } from "./quota";

export { QuotaCounter };

export interface Env {
  QUOTA: DurableObjectNamespace;
  /** e.g. https://lifescope.cloudflareaccess.com */
  TEAM_DOMAIN: string;
  /** The Access application's AUD tag. */
  POLICY_AUD: string;
  /** Questions allowed per signed-in address per calendar month. */
  MONTHLY_QUERY_LIMIT: string;
  /** Optional ceiling across ALL users; unset disables it. */
  GLOBAL_MONTHLY_LIMIT?: string;
  /** Comma-separated metered paths. Defaults to "/query". */
  METERED_PATHS?: string;
  /**
   * Optional explicit origin, e.g. https://jic-staging.example.net. Normally
   * unset: the Worker shares its hostname with the tunnel, so `fetch(request)`
   * already reaches the box. Set it to aim a deployment at a different origin,
   * or to exercise the quota locally against a stub.
   */
  ORIGIN_BASE_URL?: string;
}

/** Module-scope so the JWKS fetch is cached across requests on this isolate. */
let jwks: ReturnType<typeof createRemoteJWKSet> | undefined;
let jwksFor: string | undefined;

function keySet(teamDomain: string) {
  if (!jwks || jwksFor !== teamDomain) {
    jwks = createRemoteJWKSet(new URL(`${teamDomain}/cdn-cgi/access/certs`));
    jwksFor = teamDomain;
  }
  return jwks;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (!isMetered(request, url, env)) {
      return toOrigin(request, url, env);
    }

    // ── Identity ─────────────────────────────────────────────────────
    const token = request.headers.get("Cf-Access-Jwt-Assertion");
    if (!token) {
      // Access should have stopped this. Reaching here means the Worker route
      // covers a hostname the Access application does not — fail closed
      // rather than quietly serving unmetered inference.
      return problem(
        401,
        "This deployment requires sign-in. Reload the page to authenticate.",
      );
    }

    let claims: JWTPayload;
    try {
      const verified = await jwtVerify(token, keySet(env.TEAM_DOMAIN), {
        issuer: env.TEAM_DOMAIN,
        audience: env.POLICY_AUD,
      });
      claims = verified.payload;
    } catch {
      return problem(
        401,
        "Your session has expired. Reload the page to sign in again.",
      );
    }

    const email = typeof claims.email === "string" ? claims.email.trim().toLowerCase() : "";
    if (!email) {
      return problem(403, "No verified email address on this session.");
    }

    // ── Quota ────────────────────────────────────────────────────────
    const month = monthBucket();
    const limit = positiveInt(env.MONTHLY_QUERY_LIMIT, 500);
    const globalLimit = env.GLOBAL_MONTHLY_LIMIT
      ? positiveInt(env.GLOBAL_MONTHLY_LIMIT, 0)
      : 0;

    const mine = await counter(env, await identityKey(email));
    const reserved = await ask(mine, { action: "reserve", month, limit });

    if (!reserved.allowed) {
      return quotaExceeded(reserved, month, "your");
    }

    // The global ceiling is reserved second and refunds the per-user
    // reservation if it denies, so a request rejected by the global cap does
    // not also consume the individual's allowance.
    let globalStub: DurableObjectStub | undefined;
    if (globalLimit > 0) {
      globalStub = await counter(env, "__global__");
      const g = await ask(globalStub, { action: "reserve", month, limit: globalLimit });
      if (!g.allowed) {
        await ask(mine, { action: "refund", month, limit });
        return quotaExceeded(g, month, "this deployment's");
      }
    }

    // ── Origin ───────────────────────────────────────────────────────
    let response: Response;
    try {
      response = await toOrigin(request, url, env);
    } catch {
      await refundBoth(mine, globalStub, month, limit, globalLimit);
      return problem(
        502,
        "The JIC server did not respond. Your question was not counted against your limit.",
      );
    }

    // A question the box failed to answer must not cost the asker a query.
    // 524 is Cloudflare's own origin-timeout, which on CPU inference is a
    // real possibility for a long answer — see docs/1900-deployment-cloudflare.md.
    if (response.status >= 500) {
      await refundBoth(mine, globalStub, month, limit, globalLimit);
      return response;
    }

    const out = new Response(response.body, response);
    out.headers.set("X-JIC-Quota-Limit", String(limit));
    out.headers.set("X-JIC-Quota-Used", String(reserved.used));
    out.headers.set("X-JIC-Quota-Remaining", String(Math.max(0, limit - reserved.used)));
    out.headers.set("X-JIC-Quota-Reset", resetsAt(month));
    return out;
  },
};

// ── helpers ──────────────────────────────────────────────────────────

/**
 * Sends the request to the origin behind the tunnel. With no override this is
 * a plain `fetch(request)`: the Worker runs on the tunnel's own hostname, so
 * the request continues to the box it came for.
 */
function toOrigin(request: Request, url: URL, env: Env): Promise<Response> {
  if (!env.ORIGIN_BASE_URL) return fetch(request);
  const target = new URL(url.pathname + url.search, env.ORIGIN_BASE_URL);
  return fetch(new Request(target, request));
}

function isMetered(request: Request, url: URL, env: Env): boolean {
  if (request.method !== "POST") return false;
  const paths = (env.METERED_PATHS ?? "/query")
    .split(",")
    .map((p) => p.trim())
    .filter(Boolean);
  return paths.includes(url.pathname);
}

function counter(env: Env, name: string): Promise<DurableObjectStub> {
  return Promise.resolve(env.QUOTA.get(env.QUOTA.idFromName(name)));
}

async function ask(
  stub: DurableObjectStub,
  body: { action: "reserve" | "refund" | "peek"; month: string; limit: number },
): Promise<QuotaReply> {
  const res = await stub.fetch("https://quota.internal/", {
    method: "POST",
    body: JSON.stringify(body),
  });
  return (await res.json()) as QuotaReply;
}

async function refundBoth(
  mine: DurableObjectStub,
  globalStub: DurableObjectStub | undefined,
  month: string,
  limit: number,
  globalLimit: number,
): Promise<void> {
  await ask(mine, { action: "refund", month, limit });
  if (globalStub) {
    await ask(globalStub, { action: "refund", month, limit: globalLimit });
  }
}

/**
 * The quota store is keyed by a hash, never the address itself. Access
 * already holds the verified identity — that is where email collection
 * belongs — so the counter has no reason to keep a second copy of everyone's
 * email lying around.
 */
async function identityKey(email: string): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(email));
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/** "YYYY-MM" in UTC. Documented as UTC so a reset time is unambiguous. */
function monthBucket(): string {
  return new Date().toISOString().slice(0, 7);
}

/** ISO timestamp of 00:00 UTC on the first of the following month. */
function resetsAt(month: string): string {
  const [y, m] = month.split("-").map(Number);
  return new Date(Date.UTC(m === 12 ? y + 1 : y, m === 12 ? 0 : m, 1)).toISOString();
}

function positiveInt(raw: string | undefined, fallback: number): number {
  const n = Number.parseInt(raw ?? "", 10);
  return Number.isFinite(n) && n > 0 ? n : fallback;
}

function quotaExceeded(q: QuotaReply, month: string, whose: string): Response {
  const when = new Date(resetsAt(month)).toLocaleDateString("en-GB", {
    day: "numeric",
    month: "long",
    year: "numeric",
    timeZone: "UTC",
  });
  const res = problem(
    429,
    `You have used all ${q.limit} of ${whose} questions for this month. ` +
      `The allowance resets on ${when}.`,
  );
  res.headers.set("X-JIC-Quota-Limit", String(q.limit));
  res.headers.set("X-JIC-Quota-Used", String(q.used));
  res.headers.set("X-JIC-Quota-Remaining", "0");
  res.headers.set("X-JIC-Quota-Reset", resetsAt(month));
  // Seconds until the allowance resets, for well-behaved clients.
  res.headers.set(
    "Retry-After",
    String(Math.max(1, Math.floor((Date.parse(resetsAt(month)) - Date.now()) / 1000))),
  );
  return res;
}

/**
 * `error` is the field name the JIC UI reads (public/app.js), so the message
 * renders in the chat transcript instead of a bare status code.
 */
function problem(status: number, message: string): Response {
  return new Response(JSON.stringify({ error: message }), {
    status,
    headers: { "content-type": "application/json" },
  });
}
