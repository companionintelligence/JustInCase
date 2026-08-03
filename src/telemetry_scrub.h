#pragma once
// ── Telemetry event scrubber (structural layer) ──────────────────────
//
// Decides *which* parts of a Sentry event get fed through the redaction
// primitives in telemetry_redact.h. This is the C++ twin of
// CI-Server backend/apps/api/src/common/telemetry/scrubEvent.ts.
//
// Why this works on nlohmann::json rather than on `sentry_value_t`:
// sentry-native exposes `sentry_value_get_by_key` and list indexing, but there
// is **no public API to enumerate the keys of an object**. A scrubber that
// cannot enumerate keys cannot apply the sensitive-key rule to `extra`,
// `vars`, `tags`, request headers or breadcrumb `data` — which is where the
// reference implementation found most of its leaks. So the sentry binding
// serialises the event with `sentry_value_to_json`, scrubs it here, and
// rebuilds a `sentry_value_t` from the result. nlohmann/json is already a
// dependency of both executables, so this costs nothing at link time.
//
// Note the shapes handled below are sentry-native's, which differ from the
// JS/Python SDKs in three places the reference does not cover:
//   * `message` is an OBJECT (`{formatted: …}`), not only a string
//   * crash events carry a `threads` array whose frames need the same
//     treatment as `exception[].stacktrace.frames`
//   * `debug_meta.images[].code_file` is an absolute path on the host

#include <cstddef>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "telemetry_redact.h"

namespace jic {
namespace telemetry {

namespace scrub_detail {

using json = nlohmann::json;

// Guard against deep structures — a runaway walk in before_send would stall
// the reporting path on every captured error, and this one can run on the
// crash path.
inline constexpr int kMaxDepth = 8;

// Keys whose value is a URL. The query string carries the user's own search
// terms, so it must go even though the key itself looks innocuous — and this
// applies WHEREVER such a key appears, not only under `request`. A probe run
// against a live ingest sink caught exactly this: an `extra.url` diagnostic
// reached the wire with its query string intact while `request.url` was
// correctly stripped.
inline bool is_url_valued_key(const std::string& key) {
    const std::string k = to_lower(key);
    if (k == "url" || k == "referer" || k == "referrer" || k == "location") return true;
    return k.size() > 4 && (k.compare(k.size() - 4, 4, "_url") == 0 ||
                            k.compare(k.size() - 4, 4, "-url") == 0);
}

/// Breadcrumb `data` additionally uses these for URLs (the SDKs' own crumb
/// shape). Too generic to apply everywhere.
inline bool is_breadcrumb_url_key(const std::string& key) {
    return key == "from" || key == "to";
}

/// Generic depth-guarded walk. Objects get the sensitive-key check applied to
/// the KEY before the value is considered — passing values in one at a time
/// would skip that check and leak a bare `data.token`.
inline void scrub_value(json& value, int depth = 0) {
    if (depth > kMaxDepth) {
        value = "[Truncated]";
        return;
    }
    if (value.is_string()) {
        value = scrub_string(value.get<std::string>());
        return;
    }
    if (value.is_object()) {
        for (auto& item : value.items()) {
            if (is_sensitive_key(item.key())) {
                item.value() = "[Filtered]";
            } else if (is_url_valued_key(item.key()) && item.value().is_string()) {
                item.value() = scrub_string(scrub_url(item.value().get<std::string>()));
            } else {
                scrub_value(item.value(), depth + 1);
            }
        }
        return;
    }
    if (value.is_array()) {
        for (json& entry : value) scrub_value(entry, depth + 1);
    }
}

inline void scrub_string_field(json& parent, const char* key) {
    auto it = parent.find(key);
    if (it != parent.end() && it->is_string()) {
        *it = scrub_string(it->get<std::string>());
    }
}

inline void scrub_string_array(json& parent, const char* key) {
    auto it = parent.find(key);
    if (it != parent.end() && it->is_array()) {
        for (json& line : *it) {
            if (line.is_string()) line = scrub_string(line.get<std::string>());
        }
    }
}

/// `logentry`/`message` in object form. `message` is only the printf FORMAT
/// string; scrubbing it alone stamps "[Filtered]" on the template while
/// shipping the interpolated user data in `formatted` intact.
inline void scrub_logentry(json& entry) {
    if (entry.is_string()) {
        entry = scrub_string(entry.get<std::string>());
        return;
    }
    if (!entry.is_object()) return;
    scrub_string_field(entry, "message");
    scrub_string_field(entry, "formatted");
    auto params = entry.find("params");
    if (params != entry.end()) scrub_value(*params);
}

inline void scrub_frames(json& frames) {
    if (!frames.is_array()) return;
    for (json& frame : frames) {
        if (!frame.is_object()) continue;
        // Home-dir collapse only — do NOT path-redact `filename` further, Sentry
        // keys issue grouping on it.
        scrub_string_field(frame, "filename");
        scrub_string_field(frame, "abs_path");
        scrub_string_field(frame, "package");
        scrub_string_field(frame, "module");
        scrub_string_field(frame, "function");
        // ContextLines attaches real source text around the throw site — where
        // hardcoded keys and connection strings live.
        scrub_string_field(frame, "context_line");
        scrub_string_array(frame, "pre_context");
        scrub_string_array(frame, "post_context");
        // Captured locals in this process are query text, retrieved document
        // chunks and model output.
        auto vars = frame.find("vars");
        if (vars != frame.end()) scrub_value(*vars);
    }
}

/// Control registers only. The rest are general-purpose and, on a fault inside
/// MuPDF or llama.cpp, routinely hold up to 8 bytes of whatever the process was
/// moving at the time — a fragment of the user's document or prompt. We do not
/// symbolicate client-side and do not upload debug files, so the general
/// registers buy essentially nothing here; `pc`/`lr`/`sp`/`fp` (and the x86
/// equivalents) are the ones that anchor the stack.
inline bool is_control_register(const std::string& name) {
    const std::string n = to_lower(name);
    return n == "pc" || n == "lr" || n == "sp" || n == "fp" || n == "rip" || n == "rsp" ||
           n == "rbp" || n == "eip" || n == "esp" || n == "ebp";
}

inline void scrub_stacktrace(json& stacktrace) {
    if (!stacktrace.is_object()) return;
    if (auto frames = stacktrace.find("frames"); frames != stacktrace.end()) {
        scrub_frames(*frames);
    }
    if (auto regs = stacktrace.find("registers"); regs != stacktrace.end() && regs->is_object()) {
        json kept = json::object();
        for (const auto& reg : regs->items()) {
            if (is_control_register(reg.key())) kept[reg.key()] = reg.value();
        }
        *regs = kept;
    }
}

inline void scrub_stacktrace_holder(json& holder) {
    if (!holder.is_object()) return;
    if (auto st = holder.find("stacktrace"); st != holder.end()) scrub_stacktrace(*st);
}

/// sentry-native writes `{"values": [...]}`; older builds and the JS SDK write
/// a bare list. Return a pointer to the list either way.
inline json* values_list(json& container) {
    if (container.is_array()) return &container;
    if (container.is_object()) {
        auto it = container.find("values");
        if (it != container.end() && it->is_array()) return &(*it);
    }
    return nullptr;
}

}  // namespace scrub_detail

/// `before_send` / `on_crash` body. Mutates `event` in place.
///
/// Anything not explicitly handled here is left alone *only* when it cannot
/// carry user data (event_id, timestamps, level, platform, sdk metadata).
/// Everything that can — and every container we cannot reason about — is
/// either walked or removed.
inline void scrub_event(nlohmann::json& event) {
    using namespace scrub_detail;
    if (!event.is_object()) return;

    // ── Message ──────────────────────────────────────────────────────
    if (auto it = event.find("logentry"); it != event.end()) scrub_logentry(*it);
    if (auto it = event.find("message"); it != event.end()) scrub_logentry(*it);
    scrub_string_field(event, "transaction");
    scrub_string_field(event, "culprit");

    // ── Exceptions ───────────────────────────────────────────────────
    if (auto it = event.find("exception"); it != event.end()) {
        if (json* values = values_list(*it)) {
            for (json& exc : *values) {
                if (!exc.is_object()) continue;
                scrub_string_field(exc, "value");
                scrub_stacktrace_holder(exc);
            }
        }
    }

    // ── Threads ──────────────────────────────────────────────────────
    // sentry-native's inproc backend attaches every thread's stack to a crash
    // event. The JS/Python reference has no equivalent, so this rule is
    // specific to the native SDK — without it the crashing thread's stack is
    // scrubbed and the other 15 are not.
    if (auto it = event.find("threads"); it != event.end()) {
        if (json* values = values_list(*it)) {
            for (json& thread : *values) scrub_stacktrace_holder(thread);
        }
    }
    if (auto it = event.find("stacktrace"); it != event.end()) scrub_stacktrace(*it);

    // ── Free-form containers ─────────────────────────────────────────
    if (auto it = event.find("extra"); it != event.end()) scrub_value(*it);
    if (auto it = event.find("tags"); it != event.end()) scrub_value(*it);
    if (auto it = event.find("contexts"); it != event.end()) {
        scrub_value(*it);
        // The device name identifies the household as surely as a home path.
        if (it->is_object()) {
            if (auto dev = it->find("device"); dev != it->end() && dev->is_object()) {
                dev->erase("name");
            }
        }
    }
    if (auto it = event.find("modules"); it != event.end()) scrub_value(*it);

    // Debug images carry the on-disk path of every loaded module.
    if (auto it = event.find("debug_meta"); it != event.end() && it->is_object()) {
        if (auto images = it->find("images"); images != it->end() && images->is_array()) {
            for (json& image : *images) {
                if (!image.is_object()) continue;
                scrub_string_field(image, "code_file");
                scrub_string_field(image, "debug_file");
            }
        }
    }

    // ── Identity ─────────────────────────────────────────────────────
    // The hostname identifies the household. We never call sentry_set_user,
    // but drop the whole object rather than trust that.
    event.erase("server_name");
    event.erase("user");

    // ── Request ──────────────────────────────────────────────────────
    if (auto it = event.find("request"); it != event.end() && it->is_object()) {
        json& request = *it;
        request.erase("cookies");
        request.erase("data");
        if (auto qs = request.find("query_string"); qs != request.end() && !qs->is_null()) {
            *qs = "[Filtered]";
        }
        if (auto url = request.find("url"); url != request.end() && url->is_string()) {
            *url = scrub_url(url->get<std::string>());
        }
        if (auto headers = request.find("headers"); headers != request.end()) {
            // `send_default_pii=false` does NOT stop request headers — the
            // sensitive-key regex is the only guard. The walker also strips
            // the query string from `referer`/`referrer`/`location`.
            scrub_value(*headers);
        }
    }

    // ── Breadcrumbs ──────────────────────────────────────────────────
    // JIC adds none, but a future caller or the SDK itself might; the
    // reference implementation's single biggest leak was breadcrumbs reaching
    // Sentry verbatim.
    if (auto it = event.find("breadcrumbs"); it != event.end()) {
        if (json* crumbs = values_list(*it)) {
            for (json& crumb : *crumbs) {
                if (!crumb.is_object()) continue;
                scrub_string_field(crumb, "message");
                if (auto data = crumb.find("data"); data != crumb.end()) {
                    // The WHOLE object goes to the walker (it applies both the
                    // sensitive-key and the url-valued-key rules); only the
                    // crumb-specific `from`/`to` need a second pass.
                    scrub_value(*data);
                    if (data->is_object()) {
                        for (auto& field : data->items()) {
                            if (is_breadcrumb_url_key(field.key()) && field.value().is_string()) {
                                field.value() = scrub_url(field.value().get<std::string>());
                            }
                        }
                    }
                }
            }
        }
    }
}

}  // namespace telemetry
}  // namespace jic
