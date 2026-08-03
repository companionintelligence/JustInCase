// Unit tests for the structural scrubber (src/telemetry_scrub.h) — the
// `before_send` / `on_crash` body.
//
// Needs nlohmann/json.hpp, which this repo downloads at build time rather than
// vendoring; the Makefile fetches it into tests/unit/.deps/ (same pinned
// version as the Dockerfile). Build & run:  make -C tests/unit
//
// Each case below maps to an item on the fleet scrubber checklist, plus three
// that are specific to sentry-native's event shape (`message` as an object,
// `threads[]`, `debug_meta.images[]`).

#include <iostream>
#include <string>

#include "nlohmann/json.hpp"
#include "telemetry_scrub.h"

using json = nlohmann::json;
using jic::telemetry::scrub_event;

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

/// True when the serialised event contains none of the needles.
static bool absent(const json& event, std::initializer_list<const char*> needles) {
    const std::string dumped = event.dump();
    for (const char* needle : needles) {
        if (dumped.find(needle) != std::string::npos) {
            std::cerr << "        leaked: " << needle << std::endl;
            return false;
        }
    }
    return true;
}

// ── message / logentry ───────────────────────────────────────────────

static void test_message_object_and_string_forms() {
    // sentry-native's sentry_value_new_message_event writes an OBJECT here;
    // the JS/Python SDKs write logentry. Both must be handled, and `formatted`
    // matters more than `message`: `message` is only the format string, so
    // scrubbing it alone stamps "[Filtered]" on the template and ships the
    // interpolated user data intact.
    json event = {
        {"message", {{"message", "load failed for %s"},
                     {"formatted", "load failed for /Users/liam/docs/divorce.pdf"},
                     {"params", json::array({"/home/liam/secret.pdf"})}}},
    };
    scrub_event(event);
    CHECK_EQ(event["message"]["formatted"], "load failed for ~/docs/divorce.pdf");
    CHECK_EQ(event["message"]["params"][0], "~/secret.pdf");

    json plain = {{"message", "opened /Users/liam/x"}};
    scrub_event(plain);
    CHECK_EQ(plain["message"], "opened ~/x");

    json logentry = {{"logentry", {{"formatted", "token: sk-live-abc"}}}};
    scrub_event(logentry);
    CHECK(absent(logentry, {"sk-live-abc"}));
}

// ── exceptions & stack frames ────────────────────────────────────────

static void test_exception_frames() {
    json event = {
        {"exception",
         {{"values",
           json::array({{{"type", "std::runtime_error"},
                         {"value", "failed opening /Users/liam/data/jic.db"},
                         {"stacktrace",
                          {{"frames",
                            json::array({{{"filename", "/Users/liam/devel/jic/src/ingestion.cpp"},
                                          {"abs_path", "/Users/liam/devel/jic/src/ingestion.cpp"},
                                          {"function", "ingest"},
                                          {"context_line", "  auth = \"sk-live-abc\";"},
                                          {"pre_context", json::array({"// /home/liam/notes"})},
                                          {"post_context", json::array({"password: hunter2"})},
                                          {"vars", {{"query", "how do I treat a burn"},
                                                    {"token", "sk-live-abc"},
                                                    {"chunk_count", 12}}}}})}}}}})}}},
    };
    scrub_event(event);

    const json& frame = event["exception"]["values"][0]["stacktrace"]["frames"][0];
    // Home-dir collapse only — Sentry keys issue grouping on `filename`, so it
    // must stay recognisable.
    CHECK_EQ(frame["filename"], "~/devel/jic/src/ingestion.cpp");
    CHECK_EQ(frame["abs_path"], "~/devel/jic/src/ingestion.cpp");
    CHECK_EQ(frame["function"], "ingest");
    CHECK_EQ(frame["pre_context"][0], "// ~/notes");
    CHECK(absent(event, {"sk-live-abc", "hunter2", "/Users/liam", "/home/liam"}));
    // A sensitive KEY is filtered whatever its value looks like...
    CHECK_EQ(frame["vars"]["token"], "[Filtered]");
    // ...and an innocuous numeric diagnostic survives.
    CHECK(frame["vars"]["chunk_count"] == 12);
    // The user's question is not matched by any value pattern — which is
    // exactly why captured locals are disabled at init and why this test
    // documents that the scrubber alone would not have saved us.
    CHECK_EQ(frame["vars"]["query"], "how do I treat a burn");
}

static void test_exception_bare_list_form() {
    json event = {{"exception", json::array({{{"value", "at /Users/liam/x"}}})}};
    scrub_event(event);
    CHECK_EQ(event["exception"][0]["value"], "at ~/x");
}

static void test_registers_are_reduced_to_control_registers() {
    // A crash event from the inproc backend carries the full register file.
    // General-purpose registers hold up to 8 bytes of whatever the process was
    // moving when it faulted — inside MuPDF or llama.cpp that is a fragment of
    // the user's document or prompt.
    json event = {
        {"exception",
         {{"values",
           json::array({{{"type", "SIGSEGV"},
                         {"stacktrace",
                          {{"frames", json::array({{{"instruction_addr", "0xdead"}}})},
                           {"registers", {{"pc", "0x1"}, {"lr", "0x2"}, {"sp", "0x3"},
                                          {"fp", "0x4"}, {"x0", "0xLEAK"}, {"x11", "0xLEAK"},
                                          {"x30", "0xLEAK"}, {"rax", "0xLEAK"}}}}}}})}}},
    };
    scrub_event(event);
    const json& regs = event["exception"]["values"][0]["stacktrace"]["registers"];
    CHECK(regs.size() == 4);
    CHECK_EQ(regs["pc"], "0x1");
    CHECK_EQ(regs["lr"], "0x2");
    CHECK_EQ(regs["sp"], "0x3");
    CHECK_EQ(regs["fp"], "0x4");
    CHECK(absent(event, {"0xLEAK"}));
    // The frames themselves survive — they are what makes the report useful.
    CHECK_EQ(event["exception"]["values"][0]["stacktrace"]["frames"][0]["instruction_addr"],
             "0xdead");
}

static void test_threads_frames_are_scrubbed() {
    // sentry-native's inproc backend attaches EVERY thread's stack to a crash
    // event, not just the crashing one. The JS/Python reference has no
    // equivalent field, so this rule exists only here.
    json event = {
        {"threads",
         {{"values", json::array({{{"id", 1},
                                   {"crashed", true},
                                   {"stacktrace",
                                    {{"frames", json::array({{{"abs_path",
                                                               "/Users/liam/devel/jic/src/llm.h"},
                                                              {"context_line",
                                                               "api_key=sk-live-abc"}}})}}}}})}}},
    };
    scrub_event(event);
    CHECK(absent(event, {"sk-live-abc", "/Users/liam"}));
    CHECK_EQ(event["threads"]["values"][0]["stacktrace"]["frames"][0]["abs_path"],
             "~/devel/jic/src/llm.h");
}

// ── request ──────────────────────────────────────────────────────────

static void test_request_url_query_and_referer_together() {
    // Filtering the query string alone redacts nothing when the same content
    // rides in `url` and in `Referer`.
    json event = {
        {"request",
         {{"url", "http://localhost:8080/query?q=how+do+I+treat+a+burn"},
          {"query_string", "q=how+do+I+treat+a+burn"},
          {"method", "POST"},
          {"cookies", "session=abc"},
          {"data", {{"query", "how do I treat a burn"}}},
          {"headers",
           {{"Referer", "http://localhost:8080/search?q=my+medication+list"},
            {"Referrer", "http://localhost:8080/s?q=leak"},
            {"Location", "http://localhost:8080/l?q=leak"},
            {"X-Api-Key", "sk-live-abc"},
            {"Authorization", "Bearer eyJabc"},
            {"X-Forwarded-For", "192.0.2.9"},
            {"User-Agent", "Mozilla/5.0"}}}}},
    };
    scrub_event(event);

    const json& request = event["request"];
    CHECK_EQ(request["url"], "http://localhost:8080/query");
    CHECK_EQ(request["query_string"], "[Filtered]");
    CHECK(request.find("cookies") == request.end());
    CHECK(request.find("data") == request.end());
    CHECK_EQ(request["headers"]["Referer"], "http://localhost:8080/search");
    CHECK_EQ(request["headers"]["Referrer"], "http://localhost:8080/s");
    CHECK_EQ(request["headers"]["Location"], "http://localhost:8080/l");
    // send_default_pii=false does NOT stop request headers — the key regex is
    // the only guard.
    CHECK_EQ(request["headers"]["X-Api-Key"], "[Filtered]");
    CHECK_EQ(request["headers"]["Authorization"], "[Filtered]");
    CHECK_EQ(request["headers"]["X-Forwarded-For"], "[Filtered]");
    // ...but the anchored identity rule leaves genuine diagnostics alone.
    CHECK_EQ(request["headers"]["User-Agent"], "Mozilla/5.0");
    CHECK(absent(event, {"treat+a+burn", "medication", "sk-live-abc", "192.0.2.9"}));
}

// ── breadcrumbs ──────────────────────────────────────────────────────

static void test_breadcrumbs_message_and_whole_data_object() {
    // The whole `data` object goes through the walker. Scrubbing its values
    // one at a time would skip the sensitive-KEY check and leak a bare
    // `data.token`.
    json event = {
        {"breadcrumbs",
         {{"values",
           json::array({{{"message", "GET /Users/liam/x"},
                         {"data", {{"url", "http://h/q?q=private+question"},
                                   {"from", "/a?q=leak"},
                                   {"to", "/b#leak"},
                                   {"token", "sk-live-abc"},
                                   {"status_code", 500}}}}})}}},
    };
    scrub_event(event);

    const json& crumb = event["breadcrumbs"]["values"][0];
    CHECK_EQ(crumb["message"], "GET ~/x");
    CHECK_EQ(crumb["data"]["url"], "http://h/q");
    CHECK_EQ(crumb["data"]["from"], "/a");
    CHECK_EQ(crumb["data"]["to"], "/b");
    CHECK_EQ(crumb["data"]["token"], "[Filtered]");
    CHECK(crumb["data"]["status_code"] == 500);
    CHECK(absent(event, {"private+question", "sk-live-abc"}));

    // Bare-list shape (JS SDK) too.
    json bare = {{"breadcrumbs", json::array({{{"message", "at /home/liam/x"}}})}};
    scrub_event(bare);
    CHECK_EQ(bare["breadcrumbs"][0]["message"], "at ~/x");
}

// ── identity, contexts, debug images ─────────────────────────────────

static void test_identity_fields_removed() {
    json event = {{"server_name", "liams-macbook.local"},
                  {"user", {{"id", "42"}, {"email", "liam@example.com"}}},
                  {"contexts", {{"device", {{"name", "liams-macbook"}, {"arch", "arm64"}}},
                                {"os", {{"name", "Linux"}, {"version", "6.8.0"}}}}}};
    scrub_event(event);
    CHECK(event.find("server_name") == event.end());
    CHECK(event.find("user") == event.end());
    CHECK(event["contexts"]["device"].find("name") == event["contexts"]["device"].end());
    CHECK_EQ(event["contexts"]["device"]["arch"], "arm64");
    CHECK_EQ(event["contexts"]["os"]["name"], "Linux");
    CHECK(absent(event, {"liams-macbook", "liam@example.com"}));
}

static void test_debug_images_paths() {
    json event = {{"debug_meta",
                   {{"images", json::array({{{"type", "elf"},
                                             {"code_file", "/home/liam/build/jic-server"},
                                             {"debug_file", "/Users/liam/build/jic-server.debug"},
                                             {"image_addr", "0x400000"}}})}}}};
    scrub_event(event);
    CHECK_EQ(event["debug_meta"]["images"][0]["code_file"], "~/build/jic-server");
    CHECK_EQ(event["debug_meta"]["images"][0]["debug_file"], "~/build/jic-server.debug");
    CHECK_EQ(event["debug_meta"]["images"][0]["image_addr"], "0x400000");
}

// ── extra / tags / walker guards ─────────────────────────────────────

static void test_extra_and_tags_are_walked() {
    json event = {{"extra", {{"db_path", "/Users/liam/data/jic.db"},
                             {"api_key", "sk-live-abc"},
                             {"nested", {{"password", "hunter2"}, {"size_bytes", "10"}}}}},
                  {"tags", {{"component", "ci-just-in-case-server"},
                            {"username", "liam"}}}};
    scrub_event(event);
    CHECK_EQ(event["extra"]["db_path"], "~/data/jic.db");
    CHECK_EQ(event["extra"]["api_key"], "[Filtered]");
    CHECK_EQ(event["extra"]["nested"]["password"], "[Filtered]");
    CHECK_EQ(event["extra"]["nested"]["size_bytes"], "10");
    CHECK_EQ(event["tags"]["component"], "ci-just-in-case-server");
    CHECK_EQ(event["tags"]["username"], "[Filtered]");
    CHECK(absent(event, {"sk-live-abc", "hunter2", "/Users/liam"}));
}

static void test_url_valued_keys_are_stripped_anywhere() {
    // Regression: a probe run against a live ingest sink caught `extra.url`
    // reaching the wire with its query string intact, because URL handling
    // lived only under `request` and in breadcrumb `data`. On this appliance
    // the query string is the user's question, so the rule has to travel with
    // the KEY, wherever it appears.
    json event = {{"extra", {{"url", "http://h/query?q=PRIVATE+QUESTION"},
                             {"source_url", "http://h/s#PRIVATE"},
                             {"Referer", "http://h/r?q=PRIVATE"},
                             {"nested", {{"url", "http://h/n?q=PRIVATE"}}},
                             {"note", "keep?this"}}}};
    scrub_event(event);
    CHECK_EQ(event["extra"]["url"], "http://h/query");
    CHECK_EQ(event["extra"]["source_url"], "http://h/s");
    CHECK_EQ(event["extra"]["Referer"], "http://h/r");
    CHECK_EQ(event["extra"]["nested"]["url"], "http://h/n");
    // A '?' in the VALUE does not make the key URL-valued.
    CHECK_EQ(event["extra"]["note"], "keep?this");
    CHECK(absent(event, {"PRIVATE"}));
}

static void test_depth_guard() {
    // A runaway walk on the crash path would stall reporting on every event.
    json deep = "/Users/liam/x";
    for (int i = 0; i < 40; i++) deep = json{{"nested", deep}};
    json event = {{"extra", deep}};
    scrub_event(event);  // must terminate
    const std::string dumped = event.dump();
    CHECK(dumped.find("[Truncated]") != std::string::npos);
    CHECK(dumped.find("/Users/liam") == std::string::npos);
}

static void test_non_object_event_is_survivable() {
    json event = json::array({1, 2, 3});
    scrub_event(event);  // must not throw
    CHECK(event.is_array());
}

int main() {
    test_message_object_and_string_forms();
    test_exception_frames();
    test_exception_bare_list_form();
    test_registers_are_reduced_to_control_registers();
    test_threads_frames_are_scrubbed();
    test_request_url_query_and_referer_together();
    test_breadcrumbs_message_and_whole_data_object();
    test_identity_fields_removed();
    test_debug_images_paths();
    test_extra_and_tags_are_walked();
    test_url_valued_keys_are_stripped_anywhere();
    test_depth_guard();
    test_non_object_event_is_survivable();

    if (g_failures == 0) {
        std::cout << "All telemetry scrubber tests passed." << std::endl;
        return 0;
    }
    std::cerr << g_failures << " check(s) failed." << std::endl;
    return 1;
}
