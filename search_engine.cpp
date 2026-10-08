#include "search_engine.h"
#include "tokenizer.h"
#include "pagerank.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

SearchEngine::SearchEngine(const InvertedIndex& index, double pagerank_weight)
    : index_(index), pagerank_weight_(pagerank_weight) {
    pagerank_ = PageRank::compute(index_);
}

std::vector<SearchResult> SearchEngine::search(const std::string& query, int top_k) const {
    auto queryTerms = Tokenizer::tokenize(query);
    int N = index_.documentCount();
    if (N == 0 || queryTerms.empty()) return {};

    // Accumulate a raw TF-IDF score per candidate document (only documents
    // containing at least one query term are ever touched -- this is the
    // whole point of an inverted index: O(matching docs), not O(all docs)).
    std::unordered_map<int, double> raw_tfidf;

    for (const auto& term : queryTerms) {
        int df = index_.documentFrequency(term);
        if (df == 0) continue;
        double idf = std::log(static_cast<double>(N) / (1.0 + df));
        const auto* plist = index_.postings(term);
        if (!plist) continue;
        for (const auto& [doc_id, freq] : *plist) {
            const Document& d = index_.document(doc_id);
            double tf = d.token_count > 0
                            ? static_cast<double>(freq) / d.token_count
                            : 0.0;
            raw_tfidf[doc_id] += tf * idf;
        }
    }

    if (raw_tfidf.empty()) return {};

    double max_tfidf = 0.0;
    for (const auto& [id, score] : raw_tfidf) max_tfidf = std::max(max_tfidf, score);
    double max_pagerank = pagerank_.empty()
                               ? 1.0
                               : *std::max_element(pagerank_.begin(), pagerank_.end());
    if (max_pagerank <= 0.0) max_pagerank = 1.0;
    if (max_tfidf <= 0.0) max_tfidf = 1.0;

    std::vector<SearchResult> results;
    results.reserve(raw_tfidf.size());
    for (const auto& [doc_id, score] : raw_tfidf) {
        const Document& d = index_.document(doc_id);
        double norm_tfidf = score / max_tfidf;
        double norm_pr = pagerank_.empty() ? 0.0 : pagerank_[doc_id] / max_pagerank;
        double final_score = (1.0 - pagerank_weight_) * norm_tfidf + pagerank_weight_ * norm_pr;

        results.push_back(SearchResult{doc_id, d.url, d.title, d.snippet,
                                        norm_tfidf, norm_pr, final_score});
    }

    std::sort(results.begin(), results.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.final_score > b.final_score;
              });
    if (static_cast<int>(results.size()) > top_k) results.resize(top_k);
    return results;
}
