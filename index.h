#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

struct Document {
    int id = -1;
    std::string url;
    std::string title;
    std::string snippet;   // first ~300 chars of body text, used for result display
    int token_count = 0;   // document length, needed to normalize TF
    std::vector<int> out_links; // doc ids this page links to (within the crawled set)
};

// Thread-safe inverted index: term -> (doc_id -> term frequency).
// A single mutex guards all writes. Crawling is network-bound (each
// worker spends almost all its time waiting on curl), so the brief
// lock held only while merging one page's tokens is not a bottleneck.
class InvertedIndex {
public:
    InvertedIndex() = default;
    // A std::mutex member is neither copyable nor movable by default, so
    // InvertedIndex needs explicit move operations (e.g. Crawler::run()
    // builds one locally and returns it by value). The mutex itself is
    // never moved -- the moved-to object just gets its own fresh, unlocked
    // mutex, which is exactly what we want since nothing can be
    // concurrently accessing the old object once its data has moved out.
    InvertedIndex(InvertedIndex&& other) noexcept;
    InvertedIndex& operator=(InvertedIndex&& other) noexcept;
    InvertedIndex(const InvertedIndex&) = delete;
    InvertedIndex& operator=(const InvertedIndex&) = delete;

    // Registers a fully-parsed document and merges its tokens into the index.
    // Returns the assigned document id.
    int addDocument(const std::string& url, const std::string& title,
                     const std::string& body_text);

    // Called after all documents are added, once we know the full
    // doc_id -> out_links graph (needed for PageRank).
    void setOutLinks(int doc_id, std::vector<int> out_links);

    int documentCount() const;
    const Document& document(int id) const;
    const std::vector<Document>& documents() const { return docs_; }

    // Returns doc_id -> term frequency for a given term (empty if unseen).
    const std::unordered_map<int, int>* postings(const std::string& term) const;
    int documentFrequency(const std::string& term) const;

    // Persistence: a small pipe-delimited format, not JSON, so the
    // engine has zero extra dependencies beyond libcurl + the STL.
    void saveToDisk(const std::string& dir) const;
    static InvertedIndex loadFromDisk(const std::string& dir);

private:
    mutable std::mutex mutex_;
    std::vector<Document> docs_;
    std::unordered_map<std::string, std::unordered_map<int, int>> postings_;
};
