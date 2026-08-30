#pragma once
// ── Telemetry (Sentry) settings resolution ───────────────────────────
//
// Error reporting on the appliance is **opt-out** — on wherever a DSN is
// configured, and the user turns it off. With no `SENTRY_DSN` nothing is
// initialised and no network call is ever made. Two kill switches disable
// reporting even when a DSN has been baked into an image, so an operator can
// always turn telemetry off without rebuilding:
//
//   - `CI_TELEMETRY=off`   — explicit opt-out
//   - `CI_LOCAL_ONLY=true` — local-only mode must mean zero phone-home
//
// Precedence: local-only > opt-out > no-dsn.
//
// Mirrors CI-Server backend/apps/api/src/common/telemetry/settings.ts.
// Dependency-free on purpose: this header is unit-tested without sentry-native
// present, and it is also what the DSN-less build compiles (the whole point of
// the JIC_SENTRY CMake option is that a no-DSN binary contains no SDK at all).

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <string>

namespace jic {
namespace telemetry {

enum class DisabledReason { None, NoDsn, OptOut, LocalOnly, NotCompiledIn };

inline const char* to_string(DisabledReason reason) {
    switch (reason) {
        case DisabledReason::None:          return "none";
        case DisabledReason::NoDsn:         return "no-dsn";
        case DisabledReason::OptOut:        return "opt-out";
        case DisabledReason::LocalOnly:     return "local-only";
        case DisabledReason::NotCompiledIn: return "not-compiled-in";
    }
    return "unknown";
}

struct Settings {
    bool enabled = false;
    DisabledReason disabled_reason = DisabledReason::NoDsn;
    std::string dsn;
    std::string environment;
    std::string release;
    std::string component;
    std::string deployment_version;
    std::string database_path;
    /// Whether the fatal-signal handler (sentry-native `inproc` backend) is
    /// installed. Reporting handled errors does not require it, so it can be
    /// turned off on its own with `JIC_SENTRY_CRASH_HANDLER=off`.
    bool crash_handler = true;
};

/// Environment accessor, injectable so the gate is testable without setenv.
using EnvLookup = std::function<const char*(const char*)>;

inline EnvLookup process_env() {
    return [](const char* key) -> const char* { return std::getenv(key); };
}

namespace detail {

inline std::string trim(const char* raw) {
    if (raw == nullptr) return {};
    std::string value(raw);
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    auto begin = std::find_if(value.begin(), value.end(), not_space);
    auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return (begin < end) ? std::string(begin, end) : std::string();
}

inline std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

inline std::string read(const EnvLookup& env, const char* key) { return trim(env(key)); }

inline bool is_falsey(const std::string& value) {
    const std::string v = lower(value);
    return v == "off" || v == "false" || v == "0" || v == "no" || v == "disabled";
}

inline bool is_truthy(const std::string& value) {
    const std::string v = lower(value);
    return v == "on" || v == "true" || v == "1" || v == "yes" || v == "enabled";
}

}  // namespace detail

/// `CI_TELEMETRY=off` (any falsey spelling) opts out; anything else leaves the
/// DSN in charge.
inline bool is_telemetry_opted_out(const EnvLookup& env) {
    const std::string value = detail::read(env, "CI_TELEMETRY");
    return !value.empty() && detail::is_falsey(value);
}

/// Local-only mode forbids all egress, telemetry included.
inline bool is_local_only(const EnvLookup& env) {
    const std::string value = detail::read(env, "CI_LOCAL_ONLY");
    return !value.empty() && detail::is_truthy(value);
}

/// Directory sentry-native uses for its own state. The containers run with
/// `read_only: true`, so this has to land on a writable volume; it is derived
/// from JIC_DB_PATH's directory and kept per-component because the server and
/// the ingestion worker share the same data volume.
inline std::string default_database_path(const EnvLookup& env, const std::string& component) {
    std::string db = detail::read(env, "JIC_DB_PATH");
    if (db.empty()) db = "data/jic.db";
    const std::size_t slash = db.find_last_of('/');
    const std::string dir = (slash == std::string::npos) ? std::string(".") : db.substr(0, slash);
    return dir + "/.sentry-" + component;
}

inline Settings resolve(const EnvLookup& env, const std::string& component,
                        const std::string& compiled_version) {
    Settings s;
    s.component = component;
    s.dsn = detail::read(env, "SENTRY_DSN");

    s.deployment_version = detail::read(env, "CI_JIC_VERSION");
    if (s.deployment_version.empty()) s.deployment_version = compiled_version;

    s.environment = detail::read(env, "SENTRY_ENV");
    if (s.environment.empty()) s.environment = detail::read(env, "JIC_ENVIRONMENT");
    if (s.environment.empty()) s.environment = "development";

    s.release = detail::read(env, "SENTRY_RELEASE");
    if (s.release.empty() && !s.deployment_version.empty()) {
        s.release = component + "@" + s.deployment_version;
    }

    s.database_path = detail::read(env, "JIC_SENTRY_DB_DIR");
    if (s.database_path.empty()) s.database_path = default_database_path(env, component);

    const std::string crash = detail::read(env, "JIC_SENTRY_CRASH_HANDLER");
    s.crash_handler = crash.empty() || !detail::is_falsey(crash);

    // Order matters: report the *operator's* explicit choice ahead of "no DSN"
    // so the startup log tells them why reporting is off.
    if (is_local_only(env)) {
        s.enabled = false;
        s.disabled_reason = DisabledReason::LocalOnly;
        return s;
    }
    if (is_telemetry_opted_out(env)) {
        s.enabled = false;
        s.disabled_reason = DisabledReason::OptOut;
        return s;
    }
    if (s.dsn.empty()) {
        s.enabled = false;
        s.disabled_reason = DisabledReason::NoDsn;
        return s;
    }

    s.enabled = true;
    s.disabled_reason = DisabledReason::None;
    return s;
}

}  // namespace telemetry
}  // namespace jic
