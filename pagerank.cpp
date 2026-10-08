#include "pagerank.h"

std::vector<double> PageRank::compute(const InvertedIndex& index, double damping, int iterations) {
    int n = index.documentCount();
    if (n == 0) return {};

    std::vector<double> rank(n, 1.0 / n);
    std::vector<int> out_degree(n, 0);
    for (int i = 0; i < n; ++i) {
        out_degree[i] = static_cast<int>(index.document(i).out_links.size());
    }

    for (int iter = 0; iter < iterations; ++iter) {
        std::vector<double> next(n, (1.0 - damping) / n);

        // Dangling mass: rank held by pages with zero out-links gets
        // spread evenly across all pages, otherwise it would leak out
        // of the system and every score would slowly decay to 0.
        double dangling_mass = 0.0;
        for (int i = 0; i < n; ++i) {
            if (out_degree[i] == 0) dangling_mass += rank[i];
        }
        double dangling_share = damping * dangling_mass / n;
        for (int i = 0; i < n; ++i) next[i] += dangling_share;

        for (int i = 0; i < n; ++i) {
            if (out_degree[i] == 0) continue;
            double contribution = damping * rank[i] / out_degree[i];
            for (int target : index.document(i).out_links) {
                next[target] += contribution;
            }
        }
        rank = std::move(next);
    }
    return rank;
}
