#include "html_parser.h"
#include <cctype>
#include <algorithm>
#include <regex>

// NOTE on design: earlier versions of this file used std::regex with
// std::sregex_iterator over the *entire* page body to strip <script>/<style>
// blocks and to pull out every href. That crashed (segfault) on real pages
// in the 100KB+ range: libstdc++'s std::regex engine is a backtracking
// implementation that recurses roughly proportionally to input length for
// patterns like `[\s\S]*?`, and blows the stack well before it blows up
// asymptotically in an obviously-quadratic way. Regex is only used below on
// short, bounded strings (a single tag's contents, a URL). The whole
// document is instead walked exactly once by hand, which is both immune to
// that failure mode and faster.

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string decodeEntities(const std::string& in) {
    static const std::vector<std::pair<std::string, std::string>> kEntities = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&apos;", "'"}, {"&nbsp;", " "}, {"&mdash;", "-"},
        {"&ndash;", "-"}, {"&rsquo;", "'"}, {"&lsquo;", "'"}, {"&hellip;", "..."}
    };
    std::string out = in;
    for (auto& [entity, repl] : kEntities) {
        size_t pos = 0;
        while ((pos = out.find(entity, pos)) != std::string::npos) {
            out.replace(pos, entity.size(), repl);
            pos += repl.size();
        }
    }
    return out;
}

// Case-insensitive search for `needle` in `haystack` starting at `from`.
size_t ifind(const std::string& haystack, const std::string& needle, size_t from = 0) {
    if (needle.empty()) return std::string::npos;
    auto it = std::search(haystack.begin() + std::min(from, haystack.size()), haystack.end(),
                           needle.begin(), needle.end(),
                           [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it == haystack.end() ? std::string::npos : static_cast<size_t>(it - haystack.begin());
}

// Extracts the value of an attribute (e.g. href) from a single tag's inner
// text, such as `a class="x" href='/foo?a=1' target="_blank"`. Bounded to
// one tag (a few hundred bytes at most), so a small regex here is safe.
std::string extractAttr(const std::string& tagInner, const std::string& attr) {
    size_t pos = ifind(tagInner, attr);
    while (pos != std::string::npos) {
        size_t eq = tagInner.find('=', pos + attr.size());
        bool boundaryOk = (pos == 0 || !std::isalnum((unsigned char)tagInner[pos - 1]));
        if (eq != std::string::npos && boundaryOk) {
            size_t i = eq + 1;
            while (i < tagInner.size() && std::isspace((unsigned char)tagInner[i])) ++i;
            if (i < tagInner.size() && (tagInner[i] == '"' || tagInner[i] == '\'')) {
                char quote = tagInner[i];
                size_t end = tagInner.find(quote, i + 1);
                if (end != std::string::npos) return tagInner.substr(i + 1, end - i - 1);
            } else {
                size_t start = i;
                while (i < tagInner.size() && !std::isspace((unsigned char)tagInner[i])) ++i;
                return tagInner.substr(start, i - start);
            }
        }
        pos = ifind(tagInner, attr, pos + attr.size());
    }
    return "";
}

} // namespace

std::string HtmlParser::resolveUrl(const std::string& base, const std::string& href) {
    if (href.empty()) return "";
    if (href.rfind("javascript:", 0) == 0 || href.rfind("mailto:", 0) == 0 ||
        href.rfind("tel:", 0) == 0 || href.rfind("#", 0) == 0) {
        return "";
    }
    if (href.rfind("http://", 0) == 0 || href.rfind("https://", 0) == 0) {
        return href;
    }
    // base is a single URL (well under 200 chars) so a regex here never
    // touches more than a tiny, bounded string.
    static const std::regex schemeRe(R"(^(https?://[^/]+))");
    std::smatch m;
    if (!std::regex_search(base, m, schemeRe)) return "";
    std::string origin = m[1];

    if (href.rfind("//", 0) == 0) {
        std::string scheme = base.rfind("https://", 0) == 0 ? "https:" : "http:";
        return scheme + href;
    }
    if (href[0] == '/') {
        return origin + href;
    }
    std::string path = base.substr(origin.size());
    size_t lastSlash = path.find_last_of('/');
    std::string dir = (lastSlash == std::string::npos) ? "/" : path.substr(0, lastSlash + 1);
    return origin + dir + href;
}

ParsedPage HtmlParser::parse(const std::string& html, const std::string& base_url) {
    ParsedPage page;
    std::string text;
    text.reserve(html.size() / 2);

    size_t n = html.size();
    size_t i = 0;
    bool capturingTitle = false;
    bool titleDone = false;

    while (i < n) {
        if (html[i] != '<') {
            char c = html[i];
            if (capturingTitle) {
                page.title.push_back(c);
            } else {
                text.push_back(std::isspace((unsigned char)c) ? ' ' : c);
            }
            ++i;
            continue;
        }

        size_t tagEnd = html.find('>', i + 1);
        if (tagEnd == std::string::npos) break;

        std::string tagInner = html.substr(i + 1, tagEnd - i - 1);
        bool isClosing = !tagInner.empty() && tagInner[0] == '/';
        std::string rest = isClosing ? tagInner.substr(1) : tagInner;

        size_t nameLen = 0;
        while (nameLen < rest.size() &&
               (std::isalnum((unsigned char)rest[nameLen]) || rest[nameLen] == '-')) {
            ++nameLen;
        }
        std::string tagName = toLower(rest.substr(0, nameLen));

        if (tagName == "script" || tagName == "style") {
            std::string closeTag = "</" + tagName;
            size_t closePos = ifind(html, closeTag, tagEnd + 1);
            if (closePos == std::string::npos) { i = n; break; }
            size_t closeEnd = html.find('>', closePos);
            i = (closeEnd == std::string::npos) ? n : closeEnd + 1;
            continue;
        }

        if (tagName == "title" && !isClosing && !titleDone) {
            capturingTitle = true;
        } else if (tagName == "title" && isClosing) {
            capturingTitle = false;
            titleDone = true;
        }

        if (tagName == "a" && !isClosing) {
            std::string href = extractAttr(rest, "href");
            std::string resolved = resolveUrl(base_url, href);
            if (!resolved.empty()) page.links.push_back(resolved);
        }

        text.push_back(' ');
        i = tagEnd + 1;
    }

    page.title = decodeEntities(page.title);
    text = decodeEntities(text);

    std::string collapsed;
    collapsed.reserve(text.size());
    bool lastSpace = false;
    for (char c : text) {
        bool isSpace = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (isSpace) {
            if (!lastSpace) collapsed.push_back(' ');
            lastSpace = true;
        } else {
            collapsed.push_back(c);
            lastSpace = false;
        }
    }
    page.text = collapsed;
    return page;
}
