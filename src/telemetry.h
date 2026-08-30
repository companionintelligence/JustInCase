#pragma once
// ── JIC telemetry (Sentry) binding ───────────────────────────────────
//
// Opt-out crash/error reporting for the JIC server and ingestion worker.
// See docs/1700-error-reporting.md for the posture, and telemetry_settings.h
// for the gate.
//
// Two build modes:
//   * `JIC_SENTRY_ENABLED` undefined (the default) — every entry point below
//     compiles to a no-op and the binary contains no Sentry code at all. This
//     is what `cmake -DJIC_SENTRY=OFF` (default) produces.
//   * `JIC_SENTRY_ENABLED` defined — sentry-native is linked in.
//
// ── Crash-handler choice: inproc, no minidumps ───────────────────────
//
// sentry-native offers `crashpad`, `breakpad` and `inproc`. The first two
// capture a **minidump**: a memory image of the crashing process (thread
// stacks, registers, and heap referenced from them). JIC's address space at
// any moment holds the user's question, their conversation history, the text
// chunks retrieved from their own documents, the llama.cpp KV cache and token
// buffers, and whatever page MuPDF is parsing. The crashes most worth
// reporting — a segfault in MuPDF on a malformed PDF, a decode fault in
// llama.cpp — are exactly the ones whose memory holds the user's document and
// prompt.
//
// A minidump cannot be scrubbed on this side. `before_send`/`on_crash` operate
// on a `sentry_value_t`; the minidump is an opaque attachment written by a
// separate process (crashpad) or a raw dump writer (breakpad), and
// sentry-native's own header states the callback receives an **empty** event
// for both minidump backends. There is no client-side hook that can redact,
// inspect or truncate the dump before it leaves the appliance.
//
// `inproc` instead produces an ordinary Sentry event: signal name, a stack of
// instruction addresses, module list. No memory image. `on_crash` receives
// that event fully populated, so the scrubber sees 100% of what will be
// transmitted. That is enough to triage "SIGSEGV in the ingestion worker" or
// "SIGABRT from an uncaught exception" without shipping the user's data.
//
// So: `SENTRY_BACKEND=inproc`, no attachments, no screenshots, and the
// crashpad handler binary is never built or shipped. The reported slice is
// deliberately narrow:
//   1. handled errors raised explicitly through `capture()` below,
//   2. uncaught C++ exceptions via `std::terminate`,
//   3. fatal signals as a stack of addresses — never as a memory dump.
//
// ── What may be passed to capture() ──────────────────────────────────
//
// The scrubber is defence in depth, not the primary control. The primary
// control is that callers pass a **developer-authored constant** message and
// only bounded, non-user-derived detail (an error code, a file extension, a
// byte count). Never a query, a document filename, a chunk of extracted text,
// or a model response. Note in particular that nlohmann's parse-error strings
// quote the offending input, so a JSON parse failure on a request body must
// never be forwarded verbatim.

#include <string>
#include <utility>
#include <vector>

#include "config.h"  // JIC_VERSION
#include "telemetry_settings.h"

namespace jic {
namespace telemetry {

enum class Level { Info, Warning, Error, Fatal };

/// Bounded diagnostic detail attached to an event. Values must not be derived
/// from user content — see the header comment.
using Detail = std::vector<std::pair<std::string, std::string>>;

/// Resolve the gate and, when enabled, initialise Sentry. Returns true when
/// reporting is active. Never throws and never aborts startup: a malformed DSN
/// or an unwritable database directory disables reporting, it does not stop
/// the appliance from booting.
bool init(const std::string& component);

/// Flush and shut down, bounded by `sentry_options_set_shutdown_timeout`.
void shutdown();

bool is_active();

/// The resolved gate, available even when reporting is off (for the startup
/// log line and for the /status endpoint).
const Settings& settings();

/// One-line, log-safe summary of the gate decision.
std::string status_line();

void capture(Level level, const char* message, const Detail& detail = {});

/// Install a `std::terminate` handler that reports the uncaught exception's
/// type and (scrubbed) `what()` before re-raising. No-op when reporting is off.
void install_terminate_handler();

}  // namespace telemetry
}  // namespace jic

// ═════════════════════════════════════════════════════════════════════
// Implementation
// ═════════════════════════════════════════════════════════════════════

#include <cstdlib>
#include <exception>
#include <iostream>

#ifdef JIC_SENTRY_ENABLED
#include <sentry.h>

#include <cstdint>
#include <limits>

#include "nlohmann/json.hpp"
#include "telemetry_scrub.h"
#endif

namespace jic {
namespace telemetry {
namespace impl {

inline Settings& mutable_settings() {
    static Settings s;
    return s;
}

inline bool& active_flag() {
    static bool active = false;
    return active;
}

inline std::terminate_handler& previous_terminate() {
    static std::terminate_handler prev = nullptr;
    return prev;
}

#ifdef JIC_SENTRY_ENABLED

inline sentry_level_t to_sentry_level(Level level) {
    switch (level) {
        case Level::Info:    return SENTRY_LEVEL_INFO;
        case Level::Warning: return SENTRY_LEVEL_WARNING;
        case Level::Error:   return SENTRY_LEVEL_ERROR;
        case Level::Fatal:   return SENTRY_LEVEL_FATAL;
    }
    return SENTRY_LEVEL_ERROR;
}

/// nlohmann::json → sentry_value_t. Needed because sentry-native has no public
/// JSON parser; see the note at the top of telemetry_scrub.h for why the
/// scrubber works on JSON in the first place.
inline sentry_value_t from_json(const nlohmann::json& value) {
    switch (value.type()) {
        case nlohmann::json::value_t::boolean:
            return sentry_value_new_bool(value.get<bool>() ? 1 : 0);
        case nlohmann::json::value_t::number_integer:
            return sentry_value_new_int64(value.get<std::int64_t>());
        case nlohmann::json::value_t::number_unsigned:
            return sentry_value_new_uint64(value.get<std::uint64_t>());
        case nlohmann::json::value_t::number_float:
            return sentry_value_new_double(value.get<double>());
        case nlohmann::json::value_t::string: {
            const std::string s = value.get<std::string>();
            return sentry_value_new_string_n(s.c_str(), s.size());
        }
        case nlohmann::json::value_t::array: {
            sentry_value_t list = sentry_value_new_list();
            for (const auto& entry : value) sentry_value_append(list, from_json(entry));
            return list;
        }
        case nlohmann::json::value_t::object: {
            sentry_value_t object = sentry_value_new_object();
            for (const auto& item : value.items()) {
                sentry_value_set_by_key_n(object, item.key().c_str(), item.key().size(),
                                          from_json(item.value()));
            }
            return object;
        }
        default:
            return sentry_value_new_null();
    }
}

/// Shared body of `before_send` and `on_crash`.
///
/// **Fails closed.** If the event cannot be serialised, parsed, scrubbed or
/// rebuilt, it is discarded rather than sent unscrubbed. Losing a crash report
/// is a diagnostic cost; sending an unscrubbed one is a privacy incident.
///
/// This runs on the crashing thread for fatal signals. It allocates, which is
/// not async-signal-safe — but sentry-native's own inproc backend has already
/// allocated its way through building this event before we are called, so the
/// marginal risk is small and the alternative (no scrubbing on the crash path)
/// is not acceptable. The regexes are pre-compiled at init() so nothing lazily
/// constructs here, and the walk is depth-bounded.
inline sentry_value_t scrub_in_place(sentry_value_t event) {
    char* raw = sentry_value_to_json(event);
    sentry_value_decref(event);
    if (raw == nullptr) return sentry_value_new_null();

    sentry_value_t result = sentry_value_new_null();
    try {
        nlohmann::json parsed =
            nlohmann::json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
        if (!parsed.is_discarded()) {
            scrub_event(parsed);
            result = from_json(parsed);
        }
    } catch (...) {
        sentry_value_decref(result);
        result = sentry_value_new_null();
    }

    sentry_free(raw);
    return result;
}

inline sentry_value_t before_send_hook(sentry_value_t event, void*, void*) {
    return scrub_in_place(event);
}

/// `on_crash` is set as well as `before_send` because sentry-native stops
/// invoking `before_send` for crash events as soon as an `on_crash` exists —
/// and because on some backend/platform combinations only one of the two is
/// called. Both routes land in the same scrubber so no path can bypass it.
inline sentry_value_t on_crash_hook(const sentry_ucontext_t*, sentry_value_t event, void*) {
    return scrub_in_place(event);
}

#endif  // JIC_SENTRY_ENABLED

}  // namespace impl

inline const Settings& settings() { return impl::mutable_settings(); }

inline bool is_active() { return impl::active_flag(); }

inline std::string status_line() {
    const Settings& s = settings();
    if (s.enabled && impl::active_flag()) {
        return "error reporting: on (component=" + s.component + ", environment=" + s.environment +
               ", crash-handler=" + (s.crash_handler ? "inproc" : "off") + ", no minidumps)";
    }
    return std::string("error reporting: off (") + to_string(s.disabled_reason) + ")";
}

#ifndef JIC_SENTRY_ENABLED

// ── No-SDK build ─────────────────────────────────────────────────────
// The gate still resolves so `status_line()` can explain itself, but nothing
// can ever be enabled: there is no SDK in this binary to enable.

inline bool init(const std::string& component) {
    Settings& s = impl::mutable_settings();
    s = resolve(process_env(), component, JIC_VERSION);
    s.enabled = false;
    s.disabled_reason = DisabledReason::NotCompiledIn;
    impl::active_flag() = false;
    return false;
}

inline void shutdown() {}
inline void capture(Level, const char*, const Detail&) {}
inline void install_terminate_handler() {}

#else

inline bool init(const std::string& component) {
    Settings& s = impl::mutable_settings();
    s = resolve(process_env(), component, JIC_VERSION);
    if (!s.enabled) return false;

    // Build the regex tables now, on the startup thread, so the crash path
    // never has to.
    warm_redactor();

    sentry_options_t* options = sentry_options_new();
    if (options == nullptr) {
        s.enabled = false;
        return false;
    }

    sentry_options_set_dsn(options, s.dsn.c_str());
    sentry_options_set_environment(options, s.environment.c_str());
    if (!s.release.empty()) sentry_options_set_release(options, s.release.c_str());
    sentry_options_set_database_path(options, s.database_path.c_str());

    // Errors only. No PII, no tracing, no profiling, no sessions.
    //
    // `sentry_options_set_send_default_pii` is declared inside
    // `#ifdef SENTRY_PLATFORM_NX` in sentry-native 0.16.1 — it exists only on
    // Nintendo Switch, where it gates a pseudo-random device+app identifier.
    // On Linux there is no such identifier and no option to disable, so the
    // fleet's `sendDefaultPii: false` holds by construction here. The guard
    // matches the header's so this starts setting it if that ever changes.
#ifdef SENTRY_PLATFORM_NX
    sentry_options_set_send_default_pii(options, 0);
#endif
    sentry_options_set_traces_sample_rate(options, 0.0);
    sentry_options_set_auto_session_tracking(options, 0);
    // Nothing in JIC records breadcrumbs; keeping the ring at zero means a
    // crash event cannot carry any even if some dependency starts adding them.
    sentry_options_set_max_breadcrumbs(options, 0);
    // Reporting is opt-out at the env layer, not gated on an in-SDK consent
    // flag — `require_user_consent` would silently drop everything.
    sentry_options_set_require_user_consent(options, 0);
    // Bounded flush: shutdown must not hang the container's stop timeout.
    sentry_options_set_shutdown_timeout(options, 2000);
    sentry_options_set_debug(options, 0);
    // Pinned rather than left to the SDK default: client-side symbolication
    // calls dladdr from the signal handler, which is more non-async-signal-safe
    // work on the crash path, and buys little for a statically linked Release
    // binary. Symbolication is a debug-file-upload concern, not a runtime one.
    sentry_options_set_symbolize_stacktraces(options, 0);

    // Both hooks, same scrubber — see on_crash_hook.
    sentry_options_set_before_send(options, impl::before_send_hook, nullptr);
    sentry_options_set_on_crash(options, impl::on_crash_hook, nullptr);

    // `inproc` is selected at build time (SENTRY_BACKEND=inproc), so there is
    // no minidump writer in this binary to disable. This switch removes the
    // signal handler entirely for operators who do not want a third-party
    // handler on SIGSEGV at all; handled errors are still reported.
    if (!s.crash_handler) sentry_options_set_backend(options, nullptr);

    if (sentry_init(options) != 0) {
        std::cerr << "Sentry initialisation failed; continuing with error reporting disabled"
                  << std::endl;
        s.enabled = false;
        return false;
    }

    // Keep the scope's user null. sentry-native patches `user.id` with a
    // persisted per-install UUID (`<database>/installation_id`) whenever the
    // scope user is an object, and it does so AFTER before_send/on_crash — so
    // the scrubber cannot remove it. Never setting a user is the only control.
    sentry_remove_user();

    sentry_set_tag("component", s.component.c_str());
    if (!s.deployment_version.empty()) {
        sentry_set_tag("deployment_version", s.deployment_version.c_str());
    }

    impl::active_flag() = true;
    return true;
}

inline void shutdown() {
    if (!impl::active_flag()) return;
    impl::active_flag() = false;
    sentry_close();
}

inline void capture(Level level, const char* message, const Detail& detail) {
    if (!impl::active_flag()) return;

    sentry_value_t event = sentry_value_new_message_event(
        impl::to_sentry_level(level), settings().component.c_str(), message);

    if (!detail.empty()) {
        sentry_value_t extra = sentry_value_new_object();
        for (const auto& entry : detail) {
            sentry_value_set_by_key_n(extra, entry.first.c_str(), entry.first.size(),
                                      sentry_value_new_string_n(entry.second.c_str(),
                                                                entry.second.size()));
        }
        sentry_value_set_by_key(event, "extra", extra);
    }

    sentry_capture_event(event);
}

inline void install_terminate_handler() {
    if (!impl::active_flag()) return;
    if (impl::previous_terminate() != nullptr) return;

    impl::previous_terminate() = std::set_terminate([] {
        // An uncaught exception's type and message are far better triage than
        // the SIGABRT stack that follows, so report them explicitly. The
        // message goes through the same scrubber as everything else.
        Detail detail;
        try {
            if (std::current_exception()) std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            detail.emplace_back("exception_what", e.what());
        } catch (...) {
            detail.emplace_back("exception_what", "<non-std exception>");
        }
        capture(Level::Fatal, "uncaught exception (std::terminate)", detail);
        shutdown();

        std::terminate_handler prev = impl::previous_terminate();
        if (prev != nullptr && prev != std::get_terminate()) prev();
        std::abort();
    });
}

#endif  // JIC_SENTRY_ENABLED

}  // namespace telemetry
}  // namespace jic
