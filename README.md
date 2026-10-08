# Web Crawler + Search Engine (C++)

I made this to understand how a search engine actually works under the hood - crawling pages, building an index, and ranking results. Everything is written in C++ and the only outside library is libcurl.

It crawls a few websites using multiple threads, saves what it finds into an index, and then lets you search that index from the terminal.

## What it does

- crawls pages starting from the URLs in `seeds.txt` (stays on those domains only)
- uses a thread pool so many pages download at the same time
- strips out the HTML and keeps the text, title and links
- builds an inverted index (word -> which pages contain it)
- ranks results using TF-IDF mixed with PageRank (75% / 25%)
- saves the index to two text files so you don't need to crawl again

## What you need

- a C++17 compiler (I used g++)
- libcurl
  - Ubuntu: `sudo apt install libcurl4-openssl-dev`
  - Windows (MSYS2): `pacman -S mingw-w64-x86_64-curl`
  - Mac: `brew install curl`
- CMake is optional

## Building

With CMake:

```
mkdir build
cd build
cmake ..
cmake --build .
```

or just with g++:

```
g++ -std=c++17 -O2 *.cpp -o search_engine -lcurl -pthread
```

## Running it

Crawl (seeds file, folder to save the index, max pages, max depth, threads):

```
./search_engine crawl seeds.txt index 60 2 8
```

Search once:

```
./search_engine query index "flask web framework" 5
```

Or open an interactive prompt and keep typing queries:

```
./search_engine repl index
```

There is already a small index in the `index/` folder so you can try searching right away without crawling.

## Files

- `main.cpp` - command line stuff (crawl / query / repl)
- `crawler.cpp` - the multithreaded crawler
- `http_fetcher.cpp` - downloads a page using libcurl
- `html_parser.cpp` - pulls text and links out of HTML
- `tokenizer.cpp` - lowercases words, removes stopwords
- `index.cpp` - inverted index, plus saving and loading it
- `pagerank.cpp` - PageRank on the crawled link graph
- `search_engine.cpp` - scoring and ranking
- `thread_safe_queue.h` - the queue of URLs waiting to be crawled

## Problems I ran into

The HTML parser was the annoying part. My first version used `std::regex` to remove `<script>` and `<style>` blocks and find links, and it crashed (segfault) on bigger pages, around 100KB and above. The regex engine in libstdc++ uses a lot of stack on long inputs. I rewrote it to walk through the HTML once by hand, and regex is now only used on tiny strings like a single URL. It's faster too.

Another thing was knowing when the crawl is finished. Workers can't just quit when the queue is empty, because a page that is still being downloaded might add more links. So the queue keeps count of active items and only closes when the queue is empty and nobody is working.

## Things that are missing

- no stemming, so "crawl" and "crawling" are different words
- doesn't read robots.txt
- the whole index is kept in memory
- a single mutex on the index (fine for this size, wouldn't scale)
- the HTML parser is simple and won't handle every messy page