#include "index.h"
#include "tokenizer.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

InvertedIndex::InvertedIndex(InvertedIndex&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mutex_);
    docs_ = std::move(other.docs_);
    postings_ = std::move(other.postings_);
}

InvertedIndex& InvertedIndex::operator=(InvertedIndex&& other) noexcept {
    if (this == &other) return *this;
    std::scoped_lock lock(mutex_, other.mutex_);
    docs_ = std::move(other.docs_);
    postings_ = std::move(other.postings_);
    return *this;
}

int InvertedIndex::addDocument(const std::string& url, const std::string& title,
                                const std::string& body_text) {
    auto tokens = Tokenizer::tokenize(body_text);

    std::lock_guard<std::mutex> lock(mutex_);
    int id = static_cast<int>(docs_.size());

    Document doc;
    doc.id = id;
    doc.url = url;
    doc.title = title;
    doc.snippet = body_text.substr(0, std::min<size_t>(300, body_text.size()));
    doc.token_count = static_cast<int>(tokens.size());
    docs_.push_back(std::move(doc));

    for (const auto& t : tokens) {
        postings_[t][id] += 1;
    }
    return id;
}

void InvertedIndex::setOutLinks(int doc_id, std::vector<int> out_links) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (doc_id >= 0 && doc_id < static_cast<int>(docs_.size())) {
        docs_[doc_id].out_links = std::move(out_links);
    }
}

int InvertedIndex::documentCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(docs_.size());
}

const Document& InvertedIndex::document(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return docs_.at(id);
}

const std::unordered_map<int, int>* InvertedIndex::postings(const std::string& term) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = postings_.find(term);
    if (it == postings_.end()) return nullptr;
    return &it->second;
}

int InvertedIndex::documentFrequency(const std::string& term) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = postings_.find(term);
    if (it == postings_.end()) return 0;
    return static_cast<int>(it->second.size());
}

namespace {
// Escape '|', '\t', '\n' so the pipe-delimited format round-trips safely
// even if a page title/url happens to contain one of those bytes.
std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n' || c == '\r') out.push_back(' ');
        else if (c == '\t') out += "\\t";
        else if (c == '|') out += "\\p";
        else out.push_back(c);
    }
    return out;
}
std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == 't') { out.push_back('\t'); ++i; continue; }
            if (s[i + 1] == 'p') { out.push_back('|'); ++i; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}
}

void InvertedIndex::saveToDisk(const std::string& dir) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream docsFile(dir + "/docs.tsv");
    for (const auto& d : docs_) {
        docsFile << d.id << "\t" << escape(d.url) << "\t" << escape(d.title) << "\t"
                 << escape(d.snippet) << "\t" << d.token_count << "\t";
        for (size_t i = 0; i < d.out_links.size(); ++i) {
            if (i) docsFile << ",";
            docsFile << d.out_links[i];
        }
        docsFile << "\n";
    }

    std::ofstream postingsFile(dir + "/postings.tsv");
    for (const auto& [term, plist] : postings_) {
        postingsFile << escape(term) << "\t";
        bool first = true;
        for (const auto& [docId, freq] : plist) {
            if (!first) postingsFile << ",";
            postingsFile << docId << ":" << freq;
            first = false;
        }
        postingsFile << "\n";
    }
    std::cerr << "[index] saved " << docs_.size() << " docs, "
              << postings_.size() << " terms to " << dir << "\n";
}

InvertedIndex InvertedIndex::loadFromDisk(const std::string& dir) {
    InvertedIndex idx;
    std::ifstream docsFile(dir + "/docs.tsv");
    if (!docsFile) throw std::runtime_error("cannot open " + dir + "/docs.tsv");
    std::string line;
    while (std::getline(docsFile, line)) {
        std::stringstream ss(line);
        std::string idStr, url, title, snippet, tokenCountStr, linksStr;
        std::getline(ss, idStr, '\t');
        std::getline(ss, url, '\t');
        std::getline(ss, title, '\t');
        std::getline(ss, snippet, '\t');
        std::getline(ss, tokenCountStr, '\t');
        std::getline(ss, linksStr, '\t');

        Document d;
        d.id = std::stoi(idStr);
        d.url = unescape(url);
        d.title = unescape(title);
        d.snippet = unescape(snippet);
        d.token_count = tokenCountStr.empty() ? 0 : std::stoi(tokenCountStr);
        if (!linksStr.empty()) {
            std::stringstream ls(linksStr);
            std::string tok;
            while (std::getline(ls, tok, ',')) {
                if (!tok.empty()) d.out_links.push_back(std::stoi(tok));
            }
        }
        idx.docs_.push_back(std::move(d));
    }

    std::ifstream postingsFile(dir + "/postings.tsv");
    if (!postingsFile) throw std::runtime_error("cannot open " + dir + "/postings.tsv");
    while (std::getline(postingsFile, line)) {
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string term = unescape(line.substr(0, tab));
        std::string rest = line.substr(tab + 1);
        std::stringstream rs(rest);
        std::string pair;
        while (std::getline(rs, pair, ',')) {
            size_t colon = pair.find(':');
            if (colon == std::string::npos) continue;
            int docId = std::stoi(pair.substr(0, colon));
            int freq = std::stoi(pair.substr(colon + 1));
            idx.postings_[term][docId] = freq;
        }
    }
    return idx;
}
