#pragma once
// ── Telemetry redaction primitives ───────────────────────────────────
//
// The string/key layer of the Sentry scrubber. Deliberately free of every
// dependency (no nlohmann, no sentry-native) so it can be unit-tested with a
// bare `g++ -std=c++17` and no network — see tests/unit/test_telemetry.cpp.
//
// Ported from the fleet reference implementation
// (CI-Server backend/apps/api/src/common/telemetry/scrubEvent.ts and its
// Python twin backend/apps/hbpe/src/ci/telemetry.py). Every rule below is a
// leak that adversarial review actually found; do not "simplify" one away.
//
// The structural layer that decides *which* event fields get fed through
// here lives in telemetry_scrub.h.

#include <cctype>
#include <cstddef>
#include <regex>
#include <string>
#include <vector>

namespace jic {
namespace telemetry {

inline std::string to_lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

// Strings above this are truncated, so a stray blob (a document chunk, a
// model prompt) cannot smuggle memory contents out inside an exception
// message.
inline constexpr std::size_t kMaxStringLength = 8000;

// ── Compiled pattern tables ──────────────────────────────────────────
//
// std::regex construction is expensive and allocates. These are built once,
// on first use, and `telemetry::warm_redactor()` is called from init() so the
// construction happens at startup — never inside a signal handler.

struct Patterns {
    std::vector<std::regex> home;
    std::vector<std::regex> secret;
    std::regex sensitive_key;
    std::regex identity_key;
    std::regex url_cut;

    Patterns()
        : home{
              // Windows FIRST. The macOS pattern also matches the `/Users/<name>`
              // inside `C:/Users/<name>`; replacing that leaves a stranded
              // `C:~/…` with the drive letter still attached. Longest-prefix
              // forms have to win.
              std::regex(R"([A-Za-z]:\\Users\\[^\\/[:space:]]+)"),
              std::regex(R"([A-Za-z]:/Users/[^/[:space:]]+)"),
              std::regex(R"(/Users/[^/[:space:]]+)"),
              std::regex(R"(/home/[^/[:space:]]+)"),
          },
          secret{
              std::regex(R"(Bearer\s+[A-Za-z0-9\-._~+/]+=*)", std::regex::icase),
              // `api[-_ ]?key` (not `apikey|api_key`) so `x-api-key` matches.
              // `signature` is in the list because request-signing headers are
              // credentials too.
              std::regex(
                  R"((?:api[-_ ]?key|token|password|secret|jwt|auth|signature)[\s=:"']+[^\s"',}\]]+)",
                  std::regex::icase),
              std::regex(R"(tskey-[A-Za-z0-9-]+)", std::regex::icase),
              // Credentials in the authority section of a connection URL.
              std::regex(R"([a-z][a-z0-9+.\-]*://[^\s:@/]+:[^\s@/]+@)", std::regex::icase),
          },
          sensitive_key(
              R"(password|secret|token|authorization|cookie|jwt|api[-_ ]?key|dsn|pepper|private[-_]?key|signature)",
              std::regex::icase),
          // Keys that name a PERSON rather than carrying a credential. No value
          // pattern can match a username, so the key is the only signal.
          // Anchored so `user-agent`, `user_id` and `owner_id` survive as
          // diagnostic signal.
          identity_key(R"((?:^|[-_])user$|(?:^|[-_])owner$|username|email|forwarded|^remote-)",
                       std::regex::icase),
          url_cut(R"([?#])") {}
};

inline const Patterns& patterns() {
    static const Patterns p;
    return p;
}

/// Force the (allocating, slow) regex construction to happen now, at startup,
/// rather than lazily on the crash path.
inline void warm_redactor() { (void)patterns(); }

// ── Key classification ───────────────────────────────────────────────

/// Single predicate so credential and identity coverage cannot diverge.
inline bool is_sensitive_key(const std::string& key) {
    const Patterns& p = patterns();
    return std::regex_search(key, p.sensitive_key) || std::regex_search(key, p.identity_key);
}

// ── Value redaction ──────────────────────────────────────────────────

/// Strip query and fragment. On this appliance they carry the user's own
/// search terms, so filtering `query_string` alone redacts nothing when the
/// same content also rides in `url` or a `Referer` header.
inline std::string scrub_url(const std::string& url) {
    std::smatch m;
    if (std::regex_search(url, m, patterns().url_cut)) {
        return url.substr(0, static_cast<std::size_t>(m.position(0)));
    }
    return url;
}

inline std::string scrub_string(std::string value) {
    const Patterns& p = patterns();

    for (const std::regex& re : p.home) {
        value = std::regex_replace(value, re, "~");
    }
    for (const std::regex& re : p.secret) {
        value = std::regex_replace(value, re, "[Filtered]");
    }
    if (value.size() > kMaxStringLength) {
        value = value.substr(0, kMaxStringLength) + "… [truncated]";
    }
    return value;
}

}  // namespace telemetry
}  // namespace jic
