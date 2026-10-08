#pragma once
#include <string>
#include <vector>
#include "index.h"

struct SearchResult {
    int doc_id;
    std::string url;
    std::string title;
    std::string snippet;
    double tfidf_score;
    double pagerank_score;
    double final_score;
};

class SearchEngine {
public:
    // pagerank_weight in [0,1] controls the tf-idf vs pagerank blend:
    // final = (1 - w) * normalized_tfidf + w * normalized_pagerank
    explicit SearchEngine(const InvertedIndex& index, double pagerank_weight = 0.25);

    std::vector<SearchResult> search(const std::string& query, int top_k = 10) const;

private:
    const InvertedIndex& index_;
    std::vector<double> pagerank_;
    double pagerank_weight_;
};
