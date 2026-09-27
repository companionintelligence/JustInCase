// ══════════════════════════════════════════════════════════════════════
// QuotaCounter — a Durable Object holding one counter per calendar month.
//
// Why a Durable Object and not KV: KV is eventually consistent, so two
// concurrent questions can both read `used = 499` and both be allowed. A
// quota that silently overshoots under load is not a quota. Every request
// for a given key is serialised through one DO instance, so the
// read-modify-write below cannot interleave.
//
// One instance per identity (and one for the optional global cap), keyed by
// a hash of the email — see `identityKey()` in index.ts.
// ══════════════════════════════════════════════════════════════════════

export interface QuotaRequest {
  action: "reserve" | "refund" | "peek";
  /** Calendar month bucket, "YYYY-MM" (UTC). */
  month: string;
  limit: number;
}

export interface QuotaReply {
  allowed: boolean;
  used: number;
  limit: number;
}

export class QuotaCounter {
  private state: DurableObjectState;

  constructor(state: DurableObjectState) {
    this.state = state;
  }

  async fetch(request: Request): Promise<Response> {
    const { action, month, limit } = (await request.json()) as QuotaRequest;
    const key = `count:${month}`;
    let used = (await this.state.storage.get<number>(key)) ?? 0;

    if (action === "peek") {
      return reply({ allowed: used < limit, used, limit });
    }

    if (action === "refund") {
      // Never below zero: a refund for a month that has already rolled over
      // must not create a negative balance that hands out free queries.
      if (used > 0) {
        used -= 1;
        await this.state.storage.put(key, used);
      }
      return reply({ allowed: true, used, limit });
    }

    // reserve
    if (used >= limit) {
      return reply({ allowed: false, used, limit });
    }
    used += 1;
    await this.state.storage.put(key, used);

    // A fresh month means last month's key is dead weight. Twelve integers a
    // year is nothing, but an unbounded set of keys is still a leak, and the
    // cheapest moment to drop them is the one transition where we know a new
    // bucket just opened.
    if (used === 1) {
      const stale = await this.state.storage.list<number>({ prefix: "count:" });
      const doomed = [...stale.keys()].filter((k) => k !== key);
      if (doomed.length > 0) {
        await this.state.storage.delete(doomed);
      }
    }

    return reply({ allowed: true, used, limit });
  }
}

function reply(body: QuotaReply): Response {
  return new Response(JSON.stringify(body), {
    headers: { "content-type": "application/json" },
  });
}
