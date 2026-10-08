#include "tokenizer.h"
#include <cctype>
#include <unordered_set>
#include <algorithm>

namespace {
const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> kStop = {
        "the", "a", "an", "and", "or", "but", "if", "then", "is", "are", "was",
        "were", "be", "been", "being", "to", "of", "in", "on", "at", "for",
        "with", "by", "from", "as", "this", "that", "these", "those", "it",
        "its", "into", "about", "over", "after", "before", "between", "we",
        "you", "your", "our", "their", "his", "her", "not", "no", "can",
        "will", "would", "should", "could", "has", "have", "had", "do",
        "does", "did", "so", "than", "such", "which", "who", "what", "when",
        "where", "how", "all", "any", "each", "more", "most", "other", "some",
        "also", "up", "out", "just", "there", "here", "i"
    };
    return kStop;
}
}

bool Tokenizer::isStopword(const std::string& word) {
    return stopwords().count(word) > 0;
}

std::vector<std::string> Tokenizer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(32);

    auto flush = [&]() {
        if (current.size() >= 2 && !isStopword(current)) {
            tokens.push_back(current);
        }
        current.clear();
    };

    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}
