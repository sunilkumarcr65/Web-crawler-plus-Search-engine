#include "http_fetcher.h"
#include <curl/curl.h>

namespace {
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    out->append(ptr, total);
    return total;
}
}

HttpFetcher::HttpFetcher(long timeout_seconds) : timeout_seconds_(timeout_seconds) {}

FetchResult HttpFetcher::fetch(const std::string& url) const {
    FetchResult result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds_);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_seconds_);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // accept gzip/deflate transparently
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                      "SunilCrawler/1.0 (+educational multithreaded crawler demo)");

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        result.error = curl_easy_strerror(rc);
        curl_easy_cleanup(curl);
        return result;
    }

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    char* ct = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
    char* eff_url = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);

    result.http_code = code;
    result.content_type = ct ? ct : "";
    result.final_url = eff_url ? eff_url : url;
    result.body = std::move(body);
    result.ok = (code >= 200 && code < 300);

    curl_easy_cleanup(curl);
    return result;
}
