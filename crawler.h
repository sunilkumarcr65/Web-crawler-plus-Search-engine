#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include "index.h"
#include "thread_safe_queue.h"

struct CrawlConfig {
    int num_threads = 8;
    int max_pages = 100;
    int max_depth = 2;
    long request_timeout_seconds = 8;
    // Only URLs whose host matches one of these (or is a subdomain of one)
    // are ever fetched. This keeps the crawl inside domains this sandbox
    // is actually allowed to reach, and is good crawler hygiene generally
    // (never crawl the whole open web from a seed list unbounded).
    std::vector<std::string> allowed_domains;
};

class Crawler {
public:
    explicit Crawler(CrawlConfig config);

    // Runs the BFS crawl starting from seeds, populating and returning
    // the InvertedIndex. Blocks until the crawl completes.
    InvertedIndex run(const std::vector<std::string>& seed_urls);

private:
    struct CrawlItem { std::string url; int depth; };

    void worker(ThreadSafeQueue<CrawlItem>& frontier, InvertedIndex& index);
    bool isAllowedDomain(const std::string& url) const;
    bool markVisited(const std::string& url); // returns true if newly visited

    CrawlConfig config_;
    std::atomic<int> pages_fetched_{0};

    std::mutex visited_mutex_;
    std::unordered_set<std::string> visited_;

    // url -> doc_id, needed once crawling is done to translate each page's
    // out-going links into doc-id edges for PageRank.
    std::mutex url_to_doc_mutex_;
    std::unordered_map<std::string, int> url_to_doc_;
    std::mutex pending_links_mutex_;
    std::unordered_map<int, std::vector<std::string>> pending_links_; // doc_id -> raw link urls
};
