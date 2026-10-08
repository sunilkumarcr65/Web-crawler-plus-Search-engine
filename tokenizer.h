#pragma once
#include <string>
#include <vector>

// Lowercases, strips punctuation, splits on whitespace, drops stopwords
// and single-character tokens. Deliberately simple (no stemming) so the
// index stays easy to reason about and fast to build.
class Tokenizer {
public:
    static std::vector<std::string> tokenize(const std::string& text);
    static bool isStopword(const std::string& word);
};
