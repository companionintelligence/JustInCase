// Unit tests for the kiwix-serve response parsers (src/kiwix_client.h).
//
// The fixture below is shaped from libkiwix's OWN template,
// static/templates/search_result.xml, not from something plausible-looking:
//
//   <item>
//     <title>{{title}}</title>
//     <link>{{absolutePath}}</link>
//     {{#snippet}}  <description>{{{snippet}}}...</description>  {{/snippet}}
//     {{#bookTitle}}<book><title>{{bookTitle}}</title></book>    {{/bookTitle}}
//     {{#wordCount}}<wordCount>{{wordCount}}</wordCount>         {{/wordCount}}
//   </item>
//
// The two traps it encodes are the ones a hand-written parser gets wrong:
// the snippet is emitted with a TRIPLE stache (unescaped, so it really does
// contain <b> tags), and <book><title> sits inside the same <item> as the
// article's own <title>.
//
// Build:  make -C tests/unit test_kiwix_parse && tests/unit/test_kiwix_parse

#include <cassert>
#include <iostream>
#include <string>

#include "kiwix_client.h"

using namespace kiwix_detail;

static int failures = 0;

static void check(bool ok, const std::string& what, const std::string& got = "") {
    if (ok) { std::cout << "  ok   " << what << "\n"; return; }
    std::cout << "  FAIL " << what << (got.empty() ? "" : "  got: [" + got + "]") << "\n";
    failures++;
}

// A real-shaped two-result response: first item has every optional element,
// second has none of them.
static const char* SEARCH_XML = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:opensearch="http://a9.com/-/spec/opensearch/1.1/">
  <channel>
    <title>Search: boil water</title>
    <link>/search?books.name=wikipedia</link>
    <description>Search result for boil water</description>
    <opensearch:totalResults>2</opensearch:totalResults>
    <item>
      <title>Boiling</title>
      <link>/content/wikipedia_en_simple_all_nopic/A/Boiling</link>
        <description>Bringing water to a <b>rolling boil</b> for one minute makes it safe &amp; potable...</description>
        <book>
          <title>Simple English Wikipedia</title>
        </book>
        <wordCount>842</wordCount>
    </item>
    <item>
      <title>Water purification</title>
      <link>/content/wikipedia_en_medicine_nopic/A/Water_purification</link>
    </item>
  </channel>
</rss>)XML";

int main() {
    std::cout << "kiwix parse tests\n";

    // ── parse_search_xml ─────────────────────────────────────────────
    {
        auto hits = parse_search_xml(SEARCH_XML, 10);
        check(hits.size() == 2, "parses both items", std::to_string(hits.size()));
        if (hits.size() == 2) {
            // THE BOOK-TITLE TRAP: a naive "first <title> in the item" is
            // fine here, but a naive "last" or a scan that runs into <book>
            // would return "Simple English Wikipedia" as the article title.
            check(hits[0].title == "Boiling", "article title, not book title", hits[0].title);
            check(hits[0].book == "wikipedia_en_simple_all_nopic", "book from link", hits[0].book);
            check(hits[0].path == "A/Boiling", "in-zim path", hits[0].path);
            check(hits[0].rank == 1 && hits[1].rank == 2, "ranks are 1-based and ordered");

            // THE UNESCAPED-SNIPPET TRAP: <b> must be stripped, and the &amp;
            // entity decoded, without eating the words around them.
            check(hits[0].snippet.find("<b>") == std::string::npos, "highlight tags stripped", hits[0].snippet);
            check(hits[0].snippet.find("rolling boil") != std::string::npos, "highlighted words survive", hits[0].snippet);
            check(hits[0].snippet.find("safe & potable") != std::string::npos, "entity decoded", hits[0].snippet);

            // An item with no description/book/wordCount must still parse.
            check(hits[1].title == "Water purification", "optional-free item parses", hits[1].title);
            check(hits[1].book == "wikipedia_en_medicine_nopic", "second book", hits[1].book);
            check(hits[1].snippet.empty(), "absent description yields empty snippet", hits[1].snippet);
        }

        // The limit is a real limit, not a suggestion.
        check(parse_search_xml(SEARCH_XML, 1).size() == 1, "limit respected");

        // Degenerate inputs must yield nothing rather than crash or hang —
        // this parser runs on whatever a container returns.
        check(parse_search_xml("", 10).empty(), "empty body");
        check(parse_search_xml("<rss><channel></channel></rss>", 10).empty(), "no items");
        check(parse_search_xml("<item><title>x</title>", 10).empty(), "unterminated item");
        check(parse_search_xml("<item></item>", 10).empty(), "item with no title or link dropped");
        // A hit with a title but no resolvable path is useless for citation.
        check(parse_search_xml("<item><title>x</title><link>/nopath</link></item>", 10).empty(),
              "unsplittable link dropped");
    }

    // ── split_link ───────────────────────────────────────────────────
    {
        std::string b, p;
        split_link("/content/book_a/A/Page", b, p);
        check(b == "book_a" && p == "A/Page", "split /content/", b + " | " + p);

        b.clear(); p.clear();
        split_link("/raw/book_b/content/A/Page", b, p);
        check(b == "book_b" && p == "A/Page", "split /raw/…/content/", b + " | " + p);

        b.clear(); p.clear();
        split_link("/viewer#book_c/A/Page", b, p);
        check(b == "book_c" && p == "A/Page", "split /viewer#", b + " | " + p);
    }

    // ── decode_entities ──────────────────────────────────────────────
    {
        check(decode_entities("a &amp; b") == "a & b", "amp");
        check(decode_entities("&lt;i&gt;") == "<i>", "lt/gt");
        // ORDER MATTERS: &amp; is substituted last, so an encoded "&amp;lt;"
        // must survive as the literal "&lt;" and NOT collapse to "<". Doing
        // &amp; first silently corrupts any escaped markup.
        check(decode_entities("&amp;lt;") == "&lt;", "amp applied last", decode_entities("&amp;lt;"));
    }

    // ── strip_tags ───────────────────────────────────────────────────
    {
        check(strip_tags("<p>one</p><p>two</p>") == "one two", "tags become spaces",
              strip_tags("<p>one</p><p>two</p>"));
        check(strip_tags("  a\n\n   b  ") == "a b", "whitespace collapsed", strip_tags("  a\n\n   b  "));
    }

    // ── url_encode ───────────────────────────────────────────────────
    {
        check(url_encode("boil water") == "boil+water", "space", url_encode("boil water"));
        check(url_encode("a/b?c=d&e") == "a%2Fb%3Fc%3Dd%26e", "reserved chars", url_encode("a/b?c=d&e"));
        check(url_encode("safe-_.~") == "safe-_.~", "unreserved untouched", url_encode("safe-_.~"));
    }

    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nall passed\n");
    return failures ? 1 : 0;
}
