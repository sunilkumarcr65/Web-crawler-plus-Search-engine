#pragma once
#include <string>
#include <vector>

struct ParsedPage {
    std::string title;
    std::string text;                 // visible text, scripts/styles stripped
    std::vector<std::string> links;    // absolute URLs
};

class HtmlParser {
public:
    // base_url is used to resolve relative href="" links to absolute URLs.
    static ParsedPage parse(const std::string& html, const std::string& base_url);

private:
    static std::string resolveUrl(const std::string& base, const std::string& href);
};
