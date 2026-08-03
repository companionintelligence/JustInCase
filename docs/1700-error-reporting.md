# Error reporting (Sentry)

JIC can report crashes and handled errors to [Sentry](https://sentry.io). It is
**opt-out**: on any build configured with a DSN, reporting is **on by default**,
and the user turns it off (`CI_TELEMETRY=off`, or the **Share crash reports**
toggle on the `ci-just-in-case` app-store entry).

Two statements are both true here and easy to conflate:

- **Configured with a DSN → reporting is on** unless the user turns it off.
- **No DSN → nothing initialises**, and the process makes no outbound
  connection. That is a source checkout or any image built without
  `SENTRY_DSN`. It is the absence of configuration, not a privacy setting.

JIC goes one step further than the rest of the fleet: **without
`--build-arg JIC_SENTRY=1` there is no Sentry SDK in the binary at all.**
`src/telemetry.h` compiles every entry point to a no-op and nothing is linked.
"No DSN ⇒ no Sentry" is therefore a property of the artifact, not a promise
about its behaviour.

This implements the same env contract as CI-Server, CI-Planning,
CI-Import-Tools, CI-Hermes, CI-OpenClaw, CI-Spellbook, CI-WebXR-Time-Machine
and CI-Spatial-Companion-WebXR. The C++/`sentry-native` integration differs in
one substantial way — see [Crash handler](#crash-handler).

## Privacy posture

JIC runs a local LLM over the user's own documents, so reporting is
deliberately narrower than a Sentry default install:

| Setting | Value | Why |
|---|---|---|
| Crash backend | `inproc` | No minidumps. See below. |
| Minidumps / attachments | none | A memory image of this process is a copy of the user's questions and documents. |
| `send_default_pii` | n/a on Linux | See the note below — the fleet's `sendDefaultPii: false` holds by construction. |
| `traces_sample_rate` | `0` | Errors only. |
| `auto_session_tracking` | `0` | Release-health pings are egress we did not ask for. |
| `max_breadcrumbs` | `0` | JIC records none; the ring is pinned at zero so nothing can start. |
| `symbolize_stacktraces` | `0` | Pinned: client-side symbolication calls `dladdr` from the signal handler. |
| `before_send` / `on_crash` | scrubber | Redacts credentials and home paths, drops cookies/body/query string. |
| `shutdown_timeout` | 2000 ms | Reporting must never delay container stop. |

> **`send_default_pii` does not exist on Linux.** In sentry-native 0.16.1
> `sentry_options_set_send_default_pii` is declared inside
> `#ifdef SENTRY_PLATFORM_NX` — it exists only on Nintendo Switch, where it
> gates a pseudo-random device+app identifier. There is no equivalent
> identifier on Linux and no option to disable, so the fleet requirement holds
> without a call. `src/telemetry.h` keeps the call behind the same `#ifdef` so
> it starts being set if that ever changes. Note that on the native SDK this
> flag would never have covered request headers anyway — the scrubber's key
> regex is, as in the other repos, the only guard there.

Two kill switches disable reporting even when a DSN is baked into an image, so
an operator can turn it off without a rebuild:

| Variable | Effect |
|---|---|
| `CI_TELEMETRY=off` | Explicit opt-out (also `false`, `0`, `no`, `disabled`). |
| `CI_LOCAL_ONLY=true` | Local-only mode means zero phone-home (also `1`, `yes`, `on`, `enabled`). |

`CI_LOCAL_ONLY` takes precedence over `CI_TELEMETRY`, which takes precedence
over the DSN. `GET /status` reports the resolved decision (`telemetry.enabled`
and `telemetry.reason`) and never the DSN, so the switches can be verified on a
running container.

## Crash handler

`sentry-native` offers three crash backends. **JIC uses `inproc`, and ships no
minidumps.**

`crashpad` and `breakpad` both capture a **minidump** — a memory image of the
crashing process: thread stacks, CPU registers, and the heap reachable from
them. In JIC that memory is, at any moment:

- the question the user just asked,
- their conversation history (up to 200 conversations are retained for an hour),
- the text chunks retrieved from their own documents to build the prompt,
- the llama.cpp KV cache and the token buffers of the model's answer,
- whatever page MuPDF is currently parsing in the ingestion worker.

The crashes most worth reporting are exactly the ones where that is true: a
segfault in MuPDF on a malformed PDF, a fault in llama.cpp during decode. A
minidump of those is a copy of the user's document.

There is no client-side control that can fix this. `before_send` and `on_crash`
operate on a `sentry_value_t`; the minidump is an opaque attachment written by
a separate process (crashpad) or a raw dump writer (breakpad). sentry-native's
own header states that with both minidump backends the event handed to
`on_crash` is **empty** — the crash data is in the dump, which the callback
never sees. Server-side PII scrubbing exists in Sentry, but it runs *after* the
data has left the appliance, and an offline emergency appliance cannot make
that promise on someone else's behalf.

`inproc` instead produces an ordinary Sentry event: the signal, a stack of
instruction addresses, and the module list. No memory image, and `on_crash`
receives it fully populated, so the scrubber sees 100% of what will be
transmitted.

The reported slice is therefore narrow, and deliberately so:

1. **Handled errors** raised explicitly through `jic::telemetry::capture()` —
   index open failures, model load failures, per-document ingestion failures,
   `/query` handler failures.
2. **Uncaught C++ exceptions** via a `std::terminate` handler, which reports the
   exception type and its scrubbed `what()` — far better triage than the
   SIGABRT that follows.
3. **Fatal signals** as a stack of addresses. Enough to tell "SIGSEGV in the
   ingestion worker" from "SIGABRT from an uncaught exception" from an
   OOM kill; never a memory dump.

Set `JIC_SENTRY_CRASH_HANDLER=off` to keep (1) and (2) but install no
fatal-signal handler at all.

### What callers may pass to `capture()`

The scrubber is defence in depth, not the primary control. The primary control
is that call sites pass a **developer-authored constant** message and only
bounded, non-user-derived detail. Concretely, in this repo:

- a failed document reports its **extension, size and exception type** — never
  its filename, which is the name of one of the user's own documents, and never
  the MuPDF/SQLite message, which quotes its content;
- a failed `/query` reports the **exception type** — never `what()`, because
  nlohmann's parse errors quote the offending input, which here is the user's
  question;
- a failed model load reports the configured **GGUF filename** (operator
  configuration) — never `describe_model_path()`, which enumerates the
  directory.

## What gets scrubbed

`src/telemetry_scrub.h` (structure) and `src/telemetry_redact.h` (strings and
keys) implement the same rules as the TypeScript and Python twins:

- Bearer tokens, `api_key=` / `password:` / `token:` / `signature:` style pairs,
  Tailscale `tskey-…`, and credentials in connection URLs → `[Filtered]`
- Home directories (`/Users/<name>`, `/home/<name>`, `C:\Users\<name>`) → `~`.
  The Windows patterns run **first**: the macOS pattern also matches inside
  `C:/Users/<name>` and would leave a stranded `C:~/…`.
- Object and header keys matching
  `password|secret|token|authorization|cookie|jwt|api[-_ ]?key|dsn|pepper|private[-_]?key|signature`
  → `[Filtered]`. The hyphenated form matters: `x-api-key` is the API's own key
  header, and `send_default_pii=0` does **not** stop request headers.
- Identity-bearing keys —
  `(?:^|[-_])user$|(?:^|[-_])owner$|username|email|forwarded|^remote-` — also
  `[Filtered]`. These name a *person*; no value pattern can match a username.
  Anchored, so `user-agent`, `user_id` and `owner_id` survive as diagnostics.
- Query strings and fragments removed from `request.query_string`,
  `request.url`, the `referer` / `referrer` / `location` headers, breadcrumb
  `data.url` / `from` / `to`, **and any `url` / `*_url` key wherever it
  appears** — including `extra`. Filtering the query string alone redacts
  nothing when the same content rides in a URL somewhere else. (A probe run
  against a live ingest sink caught exactly that: `extra.url` reaching the wire
  with its query string intact while `request.url` was correctly stripped.)
- Request cookies and bodies dropped entirely.
- Stack frame `filename` / `abs_path` / `package` / `context_line` /
  `pre_context` / `post_context` and captured `vars`. `filename` gets home-dir
  collapse only — Sentry keys issue grouping on it.
- `server_name` and the whole `user` object removed; `contexts.device.name`
  removed. The hostname identifies the household like a home path does.
- Breadcrumb `message` and the **whole `data` object** passed to the walker —
  scrubbing values individually skips the sensitive-key check and leaks a bare
  `data.token`.
- `logentry.formatted` and `logentry.params`, not just `logentry.message` —
  `message` is only the printf *format string*.
- Strings over 8 KB truncated, so a stray blob cannot smuggle document contents
  out inside an exception message.

Four rules exist only in this repo, because they are shapes the JS/Python SDKs
never emit:

- **`threads[].stacktrace.frames`** — the inproc backend attaches *every*
  thread's stack to a crash event, not just the crashing one.
- **`message` as an object** — `sentry_value_new_message_event` writes
  `{formatted: …}` where the other SDKs write `logentry`.
- **`debug_meta.images[].code_file` / `debug_file`** — the on-disk path of every
  loaded module.
- **`stacktrace.registers`** — a crash event carries the whole register file.
  General-purpose registers routinely hold up to 8 bytes of whatever the
  process was moving when it faulted; inside MuPDF or llama.cpp that is a
  fragment of the user's document or prompt. Only the control registers
  (`pc`, `lr`, `sp`, `fp`, and the x86 equivalents) survive — they anchor the
  stack, and we do not symbolicate client-side, so the rest buy nothing.

### One thing the scrubber cannot reach

sentry-native persists a random UUID at `<database>/installation_id` and
**patches it into `event.user.id`** whenever the scope's user is an object —
and it does that *after* `before_send` / `on_crash`, so no callback can remove
it. It is a stable per-install identifier, exactly the thing
`sendDefaultPii: false` suppresses on the other SDKs. The only control is never
to set a scope user, so `init()` calls `sentry_remove_user()` immediately after
`sentry_init`. Verified on the wire: `"user": null` on both handled and crash
events.

### How the scrubber is implemented

sentry-native exposes `sentry_value_get_by_key` and list indexing but **no way
to enumerate the keys of an object**. A scrubber that cannot enumerate keys
cannot apply the sensitive-key rule to `extra`, `vars`, `tags`, request headers
or breadcrumb `data` — which is where the reference implementation's review
found most of its leaks. So `before_send` serialises the event with
`sentry_value_to_json`, scrubs it as `nlohmann::json` (already a dependency of
both executables), and rebuilds a `sentry_value_t`.

It **fails closed**: if the event cannot be serialised, parsed, scrubbed or
rebuilt, it is discarded rather than sent unscrubbed. Losing a crash report is
a diagnostic cost; sending an unscrubbed one is a privacy incident.

The callback can run on the crashing thread, where allocation is not
async-signal-safe. sentry-native's own inproc backend has already allocated its
way through building the event before we are called, so the marginal risk is
small; the regexes are pre-compiled at `init()` so nothing constructs lazily on
the crash path, and the walk is depth-bounded.

## Sentry project

JIC reports into the **`justincase`** project in the `companion-intelligence`
org. Both executables report into it, each tagged with its own `component`:

| Executable | `component` tag |
|---|---|
| `jic-server` | `ci-just-in-case-server` |
| `jic-ingestion` | `ci-just-in-case-ingestion` |

```
sentry issue list companion-intelligence/justincase --query "component:ci-just-in-case-ingestion"
```

## Enabling it

The SDK is a **build-time** opt-in; the DSN is a **runtime** value.

```bash
# 1. Build an image that contains the SDK (default is 0 — no SDK at all)
JIC_SENTRY=1 docker compose build

# 2. Configure the DSN at runtime (.env)
SENTRY_DSN=https://<key>@o<org>.ingest.us.sentry.io/<id>
SENTRY_ENV=production
CI_JIC_VERSION=0.3.0

docker compose up -d
```

Turning it off again, without rebuilding:

```bash
CI_TELEMETRY=off docker compose up -d
# or, for a fully air-gapped appliance:
CI_LOCAL_ONLY=true docker compose up -d
```

| Variable | Where | Meaning |
|---|---|---|
| `JIC_SENTRY` | build arg | `1` links sentry-native; `0` (default) links nothing. |
| `SENTRY_DSN` | runtime | Enables reporting. Unset ⇒ nothing initialises. |
| `SENTRY_ENV` | runtime | Falls back to `JIC_ENVIRONMENT`, then `development`. |
| `SENTRY_RELEASE` | runtime | Defaults to `<component>@<CI_JIC_VERSION or JIC_VERSION>`. |
| `CI_JIC_VERSION` | runtime | Deployment version tag. |
| `CI_TELEMETRY` | runtime | `off` disables, beating a baked-in DSN. |
| `CI_LOCAL_ONLY` | runtime | `true` disables; beats `CI_TELEMETRY`. |
| `JIC_SENTRY_CRASH_HANDLER` | runtime | `off` keeps handled-error reporting but installs no signal handler. |
| `JIC_SENTRY_DB_DIR` | runtime | SDK state dir. Defaults next to `JIC_DB_PATH`, per component — the containers run `read_only: true`, so it must land on the data volume. |

## Verifying

```bash
# The resolved gate, on a running container — never includes the DSN.
curl -s localhost:8080/status | jq .telemetry
# {"enabled": true, "reason": "none", "crash_handler": "inproc", "minidumps": false}

# The startup log line says the same thing.
docker compose logs jic | grep 'error reporting'
```

To confirm reporting is genuinely inert, start with no `SENTRY_DSN` and check
that nothing reaches `ingest.sentry.io`. In a default (`JIC_SENTRY=0`) image
there is no SDK to reach it with:

```bash
docker run --rm --entrypoint sh jic:latest -c 'strings /app/jic-server | grep -c ingest.sentry.io'
# 0
```
