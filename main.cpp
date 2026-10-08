#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include "crawler.h"
#include "search_engine.h"

namespace {

void printUsage(const char* prog) {
    std::cerr <<
        "Usage:\n"
        "  " << prog << " crawl <seeds_file> <index_dir> [max_pages] [max_depth] [threads]\n"
        "  " << prog << " query <index_dir> \"<query text>\" [top_k]\n"
        "  " << prog << " repl  <index_dir>\n";
}

std::vector<std::string> readLines(const std::string& path) {
    std::ifstream f(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line[0] != '#') lines.push_back(line);
    }
    return lines;
}

std::string hostOf(const std::string& url) {
    auto p = url.find("://");
    if (p == std::string::npos) return url;
    auto start = p + 3;
    auto end = url.find('/', start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    // strip to registrable-ish domain: keep last two labels, e.g. www.npmjs.com -> npmjs.com
    return host;
}

void printResults(const std::vector<SearchResult>& results) {
    if (results.empty()) {
        std::cout << "  (no matches)\n";
        return;
    }
    int rank = 1;
    for (const auto& r : results) {
        std::cout << "  " << rank++ << ". " << (r.title.empty() ? r.url : r.title) << "\n"
                  << "     " << r.url << "\n"
                  << "     score=" << std::fixed << std::setprecision(4) << r.final_score
                  << "  (tfidf=" << r.tfidf_score << ", pagerank=" << r.pagerank_score << ")\n"
                  << "     " << r.snippet.substr(0, 140) << "...\n\n";
    }
}

int doCrawl(int argc, char** argv) {
    if (argc < 4) { printUsage(argv[0]); return 1; }
    std::string seedsFile = argv[2];
    std::string indexDir = argv[3];
    int maxPages = argc > 4 ? std::stoi(argv[4]) : 60;
    int maxDepth = argc > 5 ? std::stoi(argv[5]) : 2;
    int threads = argc > 6 ? std::stoi(argv[6]) : 8;

    auto seeds = readLines(seedsFile);
    if (seeds.empty()) {
        std::cerr << "No seed URLs found in " << seedsFile << "\n";
        return 1;
    }

    CrawlConfig config;
    config.num_threads = threads;
    config.max_pages = maxPages;
    config.max_depth = maxDepth;
    for (const auto& s : seeds) config.allowed_domains.push_back(hostOf(s));

    std::cerr << "Seeds: " << seeds.size() << "  threads=" << threads
              << "  max_pages=" << maxPages << "  max_depth=" << maxDepth << "\n";
    std::cerr << "Allowed domains:";
    for (const auto& d : config.allowed_domains) std::cerr << " " << d;
    std::cerr << "\n";

    Crawler crawler(config);
    auto t0 = std::chrono::steady_clock::now();
    InvertedIndex index = crawler.run(seeds);
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    std::cerr << "Crawl finished in " << secs << "s ("
              << (index.documentCount() / std::max(secs, 0.001)) << " pages/sec)\n";

    std::filesystem::create_directories(indexDir);
    index.saveToDisk(indexDir);
    return 0;
}

int doQuery(int argc, char** argv) {
    if (argc < 4) { printUsage(argv[0]); return 1; }
    std::string indexDir = argv[2];
    std::string query = argv[3];
    int topK = argc > 4 ? std::stoi(argv[4]) : 5;

    InvertedIndex index = InvertedIndex::loadFromDisk(indexDir);
    SearchEngine engine(index);
    std::cout << "Query: \"" << query << "\"  (" << index.documentCount() << " pages indexed)\n\n";
    printResults(engine.search(query, topK));
    return 0;
}

int doRepl(int argc, char** argv) {
    if (argc < 3) { printUsage(argv[0]); return 1; }
    std::string indexDir = argv[2];
    InvertedIndex index = InvertedIndex::loadFromDisk(indexDir);
    SearchEngine engine(index);
    std::cout << index.documentCount() << " pages indexed. Type a query, or 'quit'.\n";
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == "quit" || line == "exit") break;
        if (line.empty()) continue;
        printResults(engine.search(line, 5));
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(argv[0]); return 1; }
    std::string mode = argv[1];
    if (mode == "crawl") return doCrawl(argc, argv);
    if (mode == "query") return doQuery(argc, argv);
    if (mode == "repl") return doRepl(argc, argv);
    printUsage(argv[0]);
    return 1;
}
