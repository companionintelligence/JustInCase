#pragma once

// ── Kiwix / ZIM library client ───────────────────────────────────────
//
// Federated retrieval against a `kiwix-serve` instance, so that a JIC answer
// can be grounded in — and cite — a ZIM library (Wikipedia, Gutenberg, the
// medical and repair packs) that is far too large to embed into the local
// index.
//
// WHY OVER HTTP AND NOT libzim.
//
// The obvious implementation is to link libzim and read the .zim files
// directly. We deliberately do not: libzim is **GPLv2-or-later** and JIC is
// MIT (see LICENSE). Linking it would relicense the binary it is linked into,
// which is a licence change for the product, not an implementation detail.
// kiwix-serve runs as its own container and we talk to it over a socket, which
// keeps the two works separate and JIC MIT. It is also how Project NOMAD
// composes the same capability — Kiwix is a service there too, not a library.
//
// WHAT THIS BUYS THAT A BUNDLE DOES NOT.
//
// Shipping Kiwix beside a chat box gives you two products in one window: a
// library you search by hand, and an assistant that has never read it. Because
// this client returns passages rather than links, kiwix hits go through the
// SAME Reciprocal Rank Fusion as the local vector and BM25 retrievers, and the
// LLM is grounded on whichever wins. A cited answer can therefore quote a
// Wikipedia article that was never ingested.
//
// EVERY CALL IS OPTIONAL AND FAILS SOFT.
//
// The library is an enhancement, never a dependency: an unset JIC_KIWIX_URL, a
// stopped container, a timeout, or a malformed response all degrade to "no
// kiwix hits" and the local corpus answers alone. This file must never be able
// to take the appliance down — the whole product promise is that it keeps
// working when things are missing.

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "httplib.h"
#include "text_utils.h"

// One hit from the ZIM library. `book`/`path` together address the article and
// are what the citation is built from.
struct KiwixHit {
    std::string book;     // ZIM name, e.g. "wikipedia_en_medicine_nopic"
    std::string path;     // in-ZIM path, e.g. "A/Dehydration"
    std::string title;    // article title
    std::string snippet;  // plain-text match snippet (highlight tags stripped)
    int         rank = 0; // 1-based position in the kiwix result list
};

// ── Pure parsing helpers ─────────────────────────────────────────────
// Free functions, not private statics, so they can be unit-tested without an
// HTTP server: everything hard about this client is in the parsing, and a
// parser you cannot run in a test is a parser nobody checks. See
// tests/unit/test_kiwix_parse.cpp.
namespace kiwix_detail {

// The five predefined XML entities. &amp; is applied LAST so that an encoded
// "&amp;lt;" survives as the literal "&lt;" rather than becoming "<" —
// decoding it first would corrupt escaped markup.
inline std::string decode_entities(const std::string& s) {
    std::string out = s;
    auto sub = [&out](const std::string& from, const std::string& to) {
        size_t p = 0;
        while ((p = out.find(from, p)) != std::string::npos) {
            out.replace(p, from.size(), to);
            p += to.size();
        }
    };
    sub("&lt;", "<");
    sub("&gt;", ">");
    sub("&quot;", "\"");
    sub("&apos;", "'");
    sub("&amp;", "&");
    return out;
}

inline std::string collapse_whitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool ws = false;
    for (char c : s) {
        const bool is_ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');
        if (is_ws) { if (!ws && !out.empty()) out.push_back(' '); ws = true; }
        else { out.push_back(c); ws = false; }
    }
    return out;
}

inline std::string strip_tags(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool in_tag = false;
    for (char c : html) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; out.push_back(' '); continue; }
        if (!in_tag) out.push_back(c);
    }
    return trim(collapse_whitespace(decode_entities(out)));
}

// First <tag>…</tag> inside `xml`, exactly as written.
inline std::string xml_raw(const std::string& xml, const std::string& tag) {
    const std::string open = "<" + tag + ">", close = "</" + tag + ">";
    const size_t a = xml.find(open);
    if (a == std::string::npos) return "";
    const size_t b = xml.find(close, a + open.size());
    if (b == std::string::npos) return "";
    return trim(xml.substr(a + open.size(), b - a - open.size()));
}

inline std::string xml_text(const std::string& xml, const std::string& tag) {
    return decode_entities(xml_raw(xml, tag));
}

// kiwix returns an absolute path. Both shapes are seen in the wild:
//   /content/<book>/<path…>      and      /viewer#<book>/<path…>
inline void split_link(const std::string& link, std::string& book, std::string& path) {
    std::string s = link;
    // ANCHORED at the start, not searched for anywhere in the string. Searching
    // got this wrong on the /raw/ form: "/raw/<book>/content/<path>" contains
    // "/content/", so a find() for it stripped the leading "/raw/<book>/" too
    // and the first path segment became the book. Prefixes are prefixes.
    for (const char* p : {"/raw/", "/content/", "/viewer#"}) {
        const std::string pre(p);
        if (s.rfind(pre, 0) == 0) { s = s.substr(pre.size()); break; }
    }
    while (!s.empty() && s.front() == '/') s.erase(0, 1);
    const size_t slash = s.find('/');
    if (slash == std::string::npos) return;
    book = s.substr(0, slash);
    path = s.substr(slash + 1);
    // /raw/<book>/content/<path> — drop the interposed segment.
    const std::string marker = "content/";
    if (path.rfind(marker, 0) == 0) path = path.substr(marker.size());
}

inline std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
        else if (c == ' ') out.push_back('+');
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 0x0F]); }
    }
    return out;
}


// Parse a kiwix-serve /search?format=xml body into hits.
//
// Schema is libkiwix's own static/templates/search_result.xml:
//   <item><title/><link/>[<description/>][<book><title/></book>][<wordCount/>]</item>
//
// TWO THINGS THAT BITE. (1) The template writes the snippet with a TRIPLE
// stache — {{{snippet}}} — i.e. UNESCAPED, so <description> legitimately
// contains raw <b> highlight markup and must be stripped, not read as text.
// (2) <book><title> means a naive "first <title> in the item" would return the
// BOOK's title for any hit that has one; the article title is taken before the
// <book> block is reached, which is why the item is truncated at <book> first.
inline std::vector<KiwixHit> parse_search_xml(const std::string& body, int limit) {
    std::vector<KiwixHit> hits;
    size_t pos = 0;
    int rank = 1;
    while (static_cast<int>(hits.size()) < limit) {
        const size_t i = body.find("<item>", pos);
        if (i == std::string::npos) break;
        const size_t end = body.find("</item>", i);
        if (end == std::string::npos) break;
        const std::string item = body.substr(i, end - i);

        // Everything before <book> — so <book><title> cannot shadow the
        // article's own <title>.
        const size_t bk = item.find("<book>");
        const std::string head = (bk == std::string::npos) ? item : item.substr(0, bk);

        KiwixHit h;
        h.title   = xml_text(head, "title");
        h.snippet = strip_tags(xml_raw(head, "description"));
        h.rank    = rank++;
        split_link(xml_text(head, "link"), h.book, h.path);
        // The <book><title> is the ZIM's human name; keep it only if the link
        // did not already yield a machine name.
        if (h.book.empty() && bk != std::string::npos)
            h.book = xml_text(item.substr(bk), "title");

        if (!h.title.empty() && !h.path.empty()) hits.push_back(h);
        pos = end + 7;
    }
    return hits;
}

}  // namespace kiwix_detail

class KiwixClient {
public:
    // Reads JIC_KIWIX_URL (e.g. "http://kiwix:8080"). Absent or empty disables
    // the whole feature, which is the default and the shipped behaviour.
    void configure(const std::string& base_url) {
        std::lock_guard<std::mutex> lock(mu_);
        base_ = trim(base_url);
        while (!base_.empty() && base_.back() == '/') base_.pop_back();
        probed_ = false;
        healthy_ = false;
    }

    bool configured() const {
        std::lock_guard<std::mutex> lock(mu_);
        return !base_.empty();
    }

    std::string base_url() const {
        std::lock_guard<std::mutex> lock(mu_);
        return base_;
    }

    // Is the library reachable right now? Cached for KIWIX_PROBE_TTL so that a
    // stopped container costs one timeout per minute rather than one per query.
    bool healthy() {
        if (!configured()) return false;
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (probed_ && now - probed_at_ < std::chrono::seconds(KIWIX_PROBE_TTL_SECONDS))
                return healthy_;
        }
        const bool ok = !books().empty();
        {
            std::lock_guard<std::mutex> lock(mu_);
            probed_    = true;
            probed_at_ = std::chrono::steady_clock::now();
            healthy_   = ok;
        }
        return ok;
    }

    // Book (ZIM) titles currently mounted, from the OPDS catalog. Used by
    // /status so the UI can say what the library actually contains, and as the
    // health probe. Empty on any failure — never throws.
    std::vector<std::string> books() {
        const std::string body = get("/catalog/v2/entries?count=" + std::to_string(KIWIX_MAX_BOOKS));
        std::vector<std::string> out;
        if (body.empty()) return out;
        // OPDS entries are <entry>…<title>NAME</title>. Take the first <title>
        // inside each <entry> and stop at KIWIX_MAX_BOOKS.
        size_t pos = 0;
        while (out.size() < static_cast<size_t>(KIWIX_MAX_BOOKS)) {
            const size_t e = body.find("<entry", pos);
            if (e == std::string::npos) break;
            const size_t end = body.find("</entry>", e);
            const std::string entry = body.substr(e, end == std::string::npos ? std::string::npos : end - e);
            const std::string t = kiwix_detail::xml_text(entry, "title");
            if (!t.empty()) out.push_back(t);
            if (end == std::string::npos) break;
            pos = end + 8;
        }
        return out;
    }

    // Full-text search across every mounted ZIM. Returns at most `limit` hits
    // in kiwix's own ranking order; `rank` is 1-based so the caller can fold it
    // into RRF exactly like the local retrievers.
    std::vector<KiwixHit> search(const std::string& query, int limit) {
        std::vector<KiwixHit> hits;
        if (query.empty() || limit <= 0) return hits;

        const std::string body = get("/search?pattern=" + kiwix_detail::url_encode(query) +
                                     "&format=xml&pageLength=" + std::to_string(limit));
        if (body.empty()) return hits;
        hits = kiwix_detail::parse_search_xml(body, limit);
        return hits;
    }

    // The article body as plain text, capped at `max_chars`. This is what gets
    // handed to the LLM as context, so it is the same shape as a local chunk.
    std::string article_text(const KiwixHit& hit, size_t max_chars) {
        if (hit.book.empty() || hit.path.empty()) return "";
        std::string html = get("/raw/" + hit.book + "/content/" + hit.path);
        if (html.empty()) html = get("/content/" + hit.book + "/" + hit.path);
        if (html.empty()) return "";
        std::string text = kiwix_detail::strip_tags(html);
        if (text.size() > max_chars) text.resize(max_chars);
        return text;
    }

private:
    // GET with a hard timeout. Any failure — unreachable, non-200, exception —
    // returns "" and is indistinguishable to the caller from "no results",
    // which is exactly the intended degradation.
    std::string get(const std::string& path) {
        std::string base;
        {
            std::lock_guard<std::mutex> lock(mu_);
            base = base_;
        }
        if (base.empty()) return "";
        try {
            httplib::Client cli(base.c_str());
            cli.set_connection_timeout(KIWIX_CONNECT_TIMEOUT_SECONDS, 0);
            cli.set_read_timeout(KIWIX_READ_TIMEOUT_SECONDS, 0);
            cli.set_follow_location(true);
            auto res = cli.Get(path.c_str());
            if (!res || res->status < 200 || res->status >= 300) return "";
            if (res->body.size() > KIWIX_MAX_RESPONSE_BYTES) return res->body.substr(0, KIWIX_MAX_RESPONSE_BYTES);
            return res->body;
        } catch (...) {
            return "";
        }
    }

    mutable std::mutex mu_;
    std::string base_;
    bool probed_  = false;
    bool healthy_ = false;
    std::chrono::steady_clock::time_point probed_at_{};
};
