#pragma once
#include "search_server.h"
#include <deque>
#include <cstdint>

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server);
    template <typename FilterFunction>
    std::vector<Document> AddFindRequest(const std::string& raw_query, FilterFunction filter_function);

    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentStatus status);

    std::vector<Document> AddFindRequest(const std::string& raw_query);

    int GetNoResultRequests() const;

private:
    struct QueryResult {
        std::string request_text;
        int results_found_count;
        std::vector<Document> results;
    };
    std::deque<QueryResult> requests_;
    int empty_requests_ = 0;
    uint64_t time_ = 0;
    const static int min_in_day_ = 1440;
    const SearchServer& server_;
};

template <typename FilterFunction>
std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query, FilterFunction filter_function) {
    time_ += 1;
    if (time_ > min_in_day_) {
        if (requests_.front().results_found_count == 0) {
            --empty_requests_;
        }
        requests_.pop_front();
    }
    auto result = server_.FindTopDocuments(raw_query, filter_function);
    if (static_cast<int>(result.size()) == 0) {
        ++empty_requests_;
    }
    requests_.push_back({ raw_query, static_cast<int>(result.size()), result });
    return result;
}