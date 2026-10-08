#pragma once
#include <vector>
#include "index.h"

// Classic iterative PageRank over the crawled link graph.
// Dangling nodes (no out-links) redistribute their rank uniformly
// to every page, which is the standard fix to keep total rank mass = 1.
class PageRank {
public:
    static std::vector<double> compute(const InvertedIndex& index,
                                        double damping = 0.85,
                                        int iterations = 30);
};
