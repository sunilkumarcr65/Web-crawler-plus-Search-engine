#pragma once
#include <string>

// Thin wrapper around libcurl for fetching a single URL.
// One curl handle is created per call so it is safe to use
// from multiple threads concurrently (libcurl easy handles
// are not thread-safe to *share*, but each thread creating
// its own handle is the standard pattern for thread pools).
struct FetchResult {
    bool ok = false;
    long http_code = 0;
    std::string body;
    std::string content_type;
    std::string final_url; // after redirects
    std::string error;
};

class HttpFetcher {
public:
    // timeout_seconds bounds both connect + total transfer time so a
    // single slow/unreachable host can never stall a whole worker thread.
    explicit HttpFetcher(long timeout_seconds = 8);
    FetchResult fetch(const std::string& url) const;

private:
    long timeout_seconds_;
};
