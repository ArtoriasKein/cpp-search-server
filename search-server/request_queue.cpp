#include "request_queue.h"
using namespace std;

RequestQueue::RequestQueue(const SearchServer& search_server)
    : server_(search_server)
{
}

std::vector<Document> RequestQueue::AddFindRequest(const string& raw_query, DocumentStatus status) {
    time_ += 1;
    if (time_ > min_in_day_) {
        if (requests_.front().results_found_count == 0) {
            --empty_requests_;
        }
        requests_.pop_front();
    }
    auto result = server_.FindTopDocuments(raw_query, status);
    if (static_cast<int>(result.size()) == 0) {
        ++empty_requests_;
    }
    requests_.push_back({ raw_query, static_cast<int>(result.size()), result });
    return result;
}

vector<Document> RequestQueue::AddFindRequest(const string& raw_query) {
    return AddFindRequest(raw_query, DocumentStatus::ACTUAL);
}

int RequestQueue::GetNoResultRequests() const {
    return empty_requests_;
}