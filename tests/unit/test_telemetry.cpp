// Unit tests for the telemetry gate (src/telemetry_settings.h) and the
// redaction primitives (src/telemetry_redact.h).
//
// Deliberately dependency-free — no nlohmann, no sentry-native, no network.
// Build & run:  make -C tests/unit
//
// Every redaction case below corresponds to a leak that adversarial review
// found in the fleet reference implementation. If one of these starts failing,
// the fix is the scrubber, not the test.

#include <cassert>
#include <iostream>
#include <map>
#include <string>

#include "telemetry_redact.h"
#include "telemetry_settings.h"

using jic::telemetry::DisabledReason;
using jic::telemetry::EnvLookup;
using jic::telemetry::is_sensitive_key;
using jic::telemetry::resolve;
using jic::telemetry::scrub_string;
using jic::telemetry::scrub_url;
using jic::telemetry::Settings;

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL  " << __func__ << ":" << __LINE__ << "  "   \
                      << #cond << std::endl;                               \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        const std::string a_ = (actual);                                       \
        const std::string e_ = (expected);                                     \
        if (a_ != e_) {                                                        \
            std::cerr << "FAIL  " << __func__ << ":" << __LINE__ << "\n"       \
                      << "        expected: " << e_ << "\n"                    \
                      << "        actual:   " << a_ << std::endl;              \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

// ── Fake environment ─────────────────────────────────────────────────

static EnvLookup env_of(const std::map<std::string, std::string>& vars) {
    // The map is copied into the closure so the lookup outlives the caller's
    // temporary, and c_str() stays valid for the life of the EnvLookup.
    auto owned = std::make_shared<std::map<std::string, std::string>>(vars);
    return [owned](const char* key) -> const char* {
        auto it = owned->find(key);
        return it == owned->end() ? nullptr : it->second.c_str();
    };
}

static const char* const kFakeDsn = "https://abc123@o1.ingest.us.sentry.io/9999999";

// ═════════════════════════════════════════════════════════════════════
// The gate
// ═════════════════════════════════════════════════════════════════════

static void test_gate_no_dsn_is_disabled() {
    Settings s = resolve(env_of({}), "ci-just-in-case-server", "0.3.0");
    CHECK(!s.enabled);
    CHECK(s.disabled_reason == DisabledReason::NoDsn);
    CHECK(s.dsn.empty());
}

static void test_gate_dsn_enables() {
    Settings s = resolve(env_of({{"SENTRY_DSN", kFakeDsn}}), "ci-just-in-case-server", "0.3.0");
    CHECK(s.enabled);
    CHECK(s.disabled_reason == DisabledReason::None);
    CHECK_EQ(s.dsn, kFakeDsn);
}

static void test_gate_opt_out_beats_dsn() {
    for (const char* spelling : {"off", "false", "0", "no", "disabled", "OFF", "Off", " off "}) {
        Settings s = resolve(env_of({{"SENTRY_DSN", kFakeDsn}, {"CI_TELEMETRY", spelling}}),
                             "ci-just-in-case-server", "0.3.0");
        CHECK(!s.enabled);
        CHECK(s.disabled_reason == DisabledReason::OptOut);
    }
}

static void test_gate_unrecognised_ci_telemetry_leaves_dsn_in_charge() {
    // Opt-OUT posture: anything that is not a falsey spelling must not disable.
    for (const char* spelling : {"on", "true", "1", "yes", "enabled", "maybe"}) {
        Settings s = resolve(env_of({{"SENTRY_DSN", kFakeDsn}, {"CI_TELEMETRY", spelling}}),
                             "ci-just-in-case-server", "0.3.0");
        CHECK(s.enabled);
    }
    // ...and with no DSN it still cannot turn reporting on.
    Settings s = resolve(env_of({{"CI_TELEMETRY", "on"}}), "ci-just-in-case-server", "0.3.0");
    CHECK(!s.enabled);
    CHECK(s.disabled_reason == DisabledReason::NoDsn);
}

static void test_gate_local_only_beats_dsn() {
    for (const char* spelling : {"true", "1", "yes", "on", "enabled", "TRUE"}) {
        Settings s = resolve(env_of({{"SENTRY_DSN", kFakeDsn}, {"CI_LOCAL_ONLY", spelling}}),
                             "ci-just-in-case-server", "0.3.0");
        CHECK(!s.enabled);
        CHECK(s.disabled_reason == DisabledReason::LocalOnly);
    }
}

static void test_gate_precedence_local_only_over_opt_out() {
    // local-only > opt-out > no-dsn: the reported reason must be the strongest.
    Settings s = resolve(env_of({{"SENTRY_DSN", kFakeDsn},
                                 {"CI_TELEMETRY", "off"},
                                 {"CI_LOCAL_ONLY", "true"}}),
                         "ci-just-in-case-server", "0.3.0");
    CHECK(!s.enabled);
    CHECK(s.disabled_reason == DisabledReason::LocalOnly);

    // No DSN + explicit opt-out reports the operator's choice, not "no-dsn".
    Settings t = resolve(env_of({{"CI_TELEMETRY", "off"}}), "ci-just-in-case-server", "0.3.0");
    CHECK(t.disabled_reason == DisabledReason::OptOut);
}

static void test_gate_blank_values_are_unset() {
    Settings s = resolve(env_of({{"SENTRY_DSN", "   "}}), "ci-just-in-case-server", "0.3.0");
    CHECK(!s.enabled);
    CHECK(s.disabled_reason == DisabledReason::NoDsn);

    // An empty CI_TELEMETRY (what compose passes for an unset variable) must
    // not be read as a falsey spelling.
    Settings t = resolve(env_of({{"SENTRY_DSN", kFakeDsn}, {"CI_TELEMETRY", ""},
                                 {"CI_LOCAL_ONLY", ""}}),
                         "ci-just-in-case-server", "0.3.0");
    CHECK(t.enabled);
}

static void test_gate_release_and_environment() {
    Settings s = resolve(env_of({{"SENTRY_DSN", kFakeDsn}}), "ci-just-in-case-server", "0.3.0");
    CHECK_EQ(s.release, "ci-just-in-case-server@0.3.0");
    CHECK_EQ(s.environment, "development");

    Settings t = resolve(env_of({{"SENTRY_DSN", kFakeDsn},
                                 {"CI_JIC_VERSION", "2026.8.2"},
                                 {"SENTRY_ENV", "production"}}),
                         "ci-just-in-case-ingestion", "0.3.0");
    CHECK_EQ(t.release, "ci-just-in-case-ingestion@2026.8.2");
    CHECK_EQ(t.environment, "production");

    Settings u = resolve(env_of({{"SENTRY_DSN", kFakeDsn}, {"SENTRY_RELEASE", "custom@1"}}),
                         "ci-just-in-case-server", "0.3.0");
    CHECK_EQ(u.release, "custom@1");
}

static void test_gate_database_path_is_per_component_and_writable() {
    // The containers run read_only:true — the SDK's state directory has to land
    // next to the index, on the one writable volume, and must not be shared
    // between the two executables.
    Settings s = resolve(env_of({{"SENTRY_DSN", kFakeDsn}}), "ci-just-in-case-server", "0.3.0");
    CHECK_EQ(s.database_path, "data/.sentry-ci-just-in-case-server");

    Settings t = resolve(env_of({{"SENTRY_DSN", kFakeDsn}, {"JIC_DB_PATH", "/app/data/jic.db"}}),
                         "ci-just-in-case-ingestion", "0.3.0");
    CHECK_EQ(t.database_path, "/app/data/.sentry-ci-just-in-case-ingestion");
    CHECK(s.database_path != t.database_path);

    Settings u = resolve(env_of({{"SENTRY_DSN", kFakeDsn}, {"JIC_SENTRY_DB_DIR", "/tmp/sn"}}),
                         "ci-just-in-case-server", "0.3.0");
    CHECK_EQ(u.database_path, "/tmp/sn");
}

static void test_gate_crash_handler_switch() {
    Settings on = resolve(env_of({{"SENTRY_DSN", kFakeDsn}}), "ci-just-in-case-server", "0.3.0");
    CHECK(on.crash_handler);

    Settings off = resolve(env_of({{"SENTRY_DSN", kFakeDsn}, {"JIC_SENTRY_CRASH_HANDLER", "off"}}),
                           "ci-just-in-case-server", "0.3.0");
    CHECK(!off.crash_handler);
    // Turning the signal handler off must NOT turn reporting off.
    CHECK(off.enabled);
}

// ═════════════════════════════════════════════════════════════════════
// Redaction primitives
// ═════════════════════════════════════════════════════════════════════

static void test_home_paths_windows_before_macos() {
    // The ORDER matters: `/Users/<name>` also matches inside `C:/Users/<name>`,
    // and replacing that first leaves a stranded `C:~/…` with the drive letter
    // still attached.
    CHECK_EQ(scrub_string("C:/Users/liam/docs/a.pdf"), "~/docs/a.pdf");
    CHECK_EQ(scrub_string("C:\\Users\\liam\\docs\\a.pdf"), "~\\docs\\a.pdf");
    CHECK_EQ(scrub_string("/Users/liam/devel/jic/src/server.cpp"), "~/devel/jic/src/server.cpp");
    CHECK_EQ(scrub_string("/home/jic/data/jic.db"), "~/data/jic.db");
    // Container paths have no account name in them and must survive intact.
    CHECK_EQ(scrub_string("/app/data/jic.db"), "/app/data/jic.db");
}

static void test_secret_patterns() {
    // The header NAME is harmless and useful; the credential is what goes.
    CHECK_EQ(scrub_string("Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.abc"),
             "Authorization: [Filtered]");
    CHECK_EQ(scrub_string("connect postgres://admin:hunter2@db:5432/x"),
             "connect [Filtered]db:5432/x");
    CHECK(scrub_string("node tskey-auth-k9F2abc").find("tskey-auth") == std::string::npos);
    CHECK(scrub_string("password: hunter2").find("hunter2") == std::string::npos);
    CHECK(scrub_string("api_key=sk-live-123").find("sk-live-123") == std::string::npos);
    CHECK(scrub_string("api-key: sk-live-123").find("sk-live-123") == std::string::npos);
    CHECK(scrub_string("\"signature\": \"deadbeef\"").find("deadbeef") == std::string::npos);
}

static void test_sensitive_keys() {
    // Credentials.
    CHECK(is_sensitive_key("password"));
    CHECK(is_sensitive_key("Authorization"));
    CHECK(is_sensitive_key("cookie"));
    CHECK(is_sensitive_key("SENTRY_DSN"));
    CHECK(is_sensitive_key("private-key"));
    CHECK(is_sensitive_key("private_key"));
    CHECK(is_sensitive_key("x-signature"));
    // `api[-_ ]?key`, not `apikey|api_key` — otherwise `x-api-key`, the header
    // an API actually uses, sails straight through.
    CHECK(is_sensitive_key("x-api-key"));
    CHECK(is_sensitive_key("api_key"));
    CHECK(is_sensitive_key("apikey"));
    CHECK(is_sensitive_key("API Key"));
}

static void test_identity_keys_are_anchored() {
    // These name a PERSON; no value pattern can match a username.
    CHECK(is_sensitive_key("user"));
    CHECK(is_sensitive_key("db_user"));
    CHECK(is_sensitive_key("owner"));
    CHECK(is_sensitive_key("username"));
    CHECK(is_sensitive_key("email"));
    CHECK(is_sensitive_key("x-forwarded-for"));
    CHECK(is_sensitive_key("remote-addr"));

    // ...but anchored, so these survive as diagnostics.
    CHECK(!is_sensitive_key("user-agent"));
    CHECK(!is_sensitive_key("user_id"));
    CHECK(!is_sensitive_key("owner_id"));
    CHECK(!is_sensitive_key("chunk_count"));
    CHECK(!is_sensitive_key("extension"));
    CHECK(!is_sensitive_key("exception_type"));
}

static void test_url_scrubbing() {
    // On this appliance the query string is the user's own question.
    CHECK_EQ(scrub_url("http://localhost:8080/query?q=how+do+I+treat+a+burn"),
             "http://localhost:8080/query");
    CHECK_EQ(scrub_url("http://localhost:8080/search#my-private-note"),
             "http://localhost:8080/search");
    CHECK_EQ(scrub_url("/api/library"), "/api/library");
}

static void test_truncation() {
    const std::string blob(jic::telemetry::kMaxStringLength + 500, 'x');
    const std::string out = scrub_string(blob);
    CHECK(out.size() < blob.size());
    CHECK(out.rfind("… [truncated]") != std::string::npos);
    // Just under the limit is left alone.
    const std::string ok(jic::telemetry::kMaxStringLength, 'x');
    CHECK(scrub_string(ok).size() == jic::telemetry::kMaxStringLength);
}

int main() {
    test_gate_no_dsn_is_disabled();
    test_gate_dsn_enables();
    test_gate_opt_out_beats_dsn();
    test_gate_unrecognised_ci_telemetry_leaves_dsn_in_charge();
    test_gate_local_only_beats_dsn();
    test_gate_precedence_local_only_over_opt_out();
    test_gate_blank_values_are_unset();
    test_gate_release_and_environment();
    test_gate_database_path_is_per_component_and_writable();
    test_gate_crash_handler_switch();

    test_home_paths_windows_before_macos();
    test_secret_patterns();
    test_sensitive_keys();
    test_identity_keys_are_anchored();
    test_url_scrubbing();
    test_truncation();

    if (g_failures == 0) {
        std::cout << "All telemetry gate/redaction tests passed." << std::endl;
        return 0;
    }
    std::cerr << g_failures << " check(s) failed." << std::endl;
    return 1;
}
