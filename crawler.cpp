#include "crawler.h"
#include "thread_safe_queue.h"
#include "http_fetcher.h"
#include "html_parser.h"
#include <thread>
#include <iostream>
#include <regex>
#include <algorithm>

Crawler::Crawler(CrawlConfig config) : config_(std::move(config)) {}

bool Crawler::isAllowedDomain(const std::string& url) const {
    if (config_.allowed_domains.empty()) return true;
    static const std::regex hostRe(R"(^https?://([^/]+))");
    std::smatch m;
    if (!std::regex_search(url, m, hostRe)) return false;
    std::string host = m[1];
    std::transform(host.begin(), host.end(), host.begin(), ::tolower);
    for (const auto& allowed : config_.allowed_domains) {
        if (host == allowed) return true;
        // subdomain match, e.g. "raw.githubusercontent.com" ends with ".github.com"? no -
        // keep it simple: allow exact host or host ending in "." + allowed.
        if (host.size() > allowed.size() &&
            host.compare(host.size() - allowed.size() - 1, allowed.size() + 1, "." + allowed) == 0) {
            return true;
        }
    }
    return false;
}

bool Crawler::markVisited(const std::string& url) {
    std::lock_guard<std::mutex> lock(visited_mutex_);
    if (visited_.count(url)) return false;
    visited_.insert(url);
    return true;
}

void Crawler::worker(ThreadSafeQueue<CrawlItem>& frontier, InvertedIndex& index) {
    HttpFetcher fetcher(config_.request_timeout_seconds);

    while (auto item = frontier.pop()) {
        // frontier.pop() already returned nullopt handling to the caller,
        // this loop body only runs for a real item.
        const std::string url = item->url;
        const int depth = item->depth;

        if (pages_fetched_.load() >= config_.max_pages) {
            frontier.markDone();
            continue;
        }

        FetchResult res = fetcher.fetch(url);
        if (!res.ok || res.content_type.find("text/html") == std::string::npos) {
            frontier.markDone();
            continue;
        }

        int fetched_so_far = ++pages_fetched_;
        if (fetched_so_far > config_.max_pages) {
            frontier.markDone();
            continue;
        }

        ParsedPage page = HtmlParser::parse(res.body, res.final_url);
        int doc_id = index.addDocument(res.final_url, page.title, page.text);

        {
            std::lock_guard<std::mutex> lock(url_to_doc_mutex_);
            url_to_doc_[res.final_url] = doc_id;
            url_to_doc_[url] = doc_id; // pre-redirect url too, so link resolution still hits
        }
        {
            std::lock_guard<std::mutex> lock(pending_links_mutex_);
            pending_links_[doc_id] = page.links;
        }

        std::cerr << "[crawl] (" << fetched_so_far << "/" << config_.max_pages
                  << ", depth " << depth << ") " << res.final_url
                  << "  [" << page.text.size() << " chars, "
                  << page.links.size() << " links]\n";

        if (depth < config_.max_depth) {
            for (const auto& link : page.links) {
                if (!isAllowedDomain(link)) continue;
                if (pages_fetched_.load() >= config_.max_pages) break;
                if (markVisited(link)) {
                    frontier.markActive();
                    frontier.push({link, depth + 1});
                }
            }
        }

        frontier.markDone();
    }
}

InvertedIndex Crawler::run(const std::vector<std::string>& seed_urls) {
    InvertedIndex index;
    ThreadSafeQueue<CrawlItem> frontier;

    for (const auto& seed : seed_urls) {
        if (markVisited(seed)) {
            frontier.markActive();
            frontier.push({seed, 0});
        }
    }

    std::vector<std::thread> pool;
    int n = std::max(1, config_.num_threads);
    pool.reserve(n);
    for (int i = 0; i < n; ++i) {
        pool.emplace_back([this, &frontier, &index] { worker(frontier, index); });
    }
    for (auto& t : pool) t.join();

    // Second pass: now that every page has a doc_id, translate each page's
    // raw out-going links into doc-id edges for PageRank. Links to pages
    // that were never fetched (out of budget / disallowed domain) are
    // simply dropped -- PageRank only needs edges within the crawled set.
    for (const auto& [doc_id, links] : pending_links_) {
        std::vector<int> out;
        for (const auto& link : links) {
            auto it = url_to_doc_.find(link);
            if (it != url_to_doc_.end() && it->second != doc_id) {
                out.push_back(it->second);
            }
        }
        index.setOutLinks(doc_id, std::move(out));
    }

    std::cerr << "[crawl] done. " << index.documentCount() << " pages indexed.\n";
    return index;
}
