#pragma once
#include <string>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
#include <thread>
#include <utility>
#include <algorithm>
#include <execution>
#include <functional>
#include <type_traits>
#include "document.h"
#include "string_processing.h"
#include "concurrent_map.h"

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const int ACCURACY = 1e-6;

class SearchServer {
public:
    typedef std::set<int>::const_iterator const it;

    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    explicit SearchServer(const std::string_view stop_words_text);
    explicit SearchServer(const std::string& stop_words_text);

    void AddDocument(int document_id, const std::string_view document, DocumentStatus status, const std::vector<int>& ratings);
    
    template <typename ExecutionPolicy, typename FilterFunction>
    std::vector<Document> FindTopDocuments(ExecutionPolicy& policy, const std::string_view raw_query, FilterFunction filter_function) const;
    template <typename FilterFunction>
    std::vector<Document> FindTopDocuments(const std::string_view raw_query, FilterFunction filter_function) const;
    std::vector<Document> FindTopDocuments(const std::string_view raw_query, DocumentStatus status) const;
    std::vector<Document> FindTopDocuments(const std::string_view raw_query) const;
    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy& policy, const std::string_view raw_query, DocumentStatus status) const;
    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy& policy, const std::string_view raw_query) const;

    int GetDocumentCount() const;

    it begin();
    it end();

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(const std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::sequenced_policy policy, const std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::parallel_policy policy, const std::string_view raw_query, int document_id) const;

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

    void RemoveDocument(int document_id);
    void RemoveDocument(std::execution::sequenced_policy polic, int document_id);
    void RemoveDocument(std::execution::parallel_policy policy, int document_id);

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::set<std::string> words_in_document;
    };
    const std::set<std::string, std::less<>> stop_words_;
    std::map<int, std::map<std::string, double, std::less<>>> document_to_word_freqs_;
    std::map<std::string_view, std::map<int, double>, std::less<>> word_to_document_freqs_;
    std::map<int, DocumentData> documents_;
    std::set<int> document_ids_;

    bool IsStopWord(std::string_view word) const;
    static bool IsValidWord(std::string_view word);
    std::vector<std::string_view> SplitIntoWordsNoStop(const std::string_view text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(const std::string_view text) const;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQuery(const std::string_view text) const;
    Query ParseQuerySorted(const std::string_view text) const;
    double ComputeWordInverseDocumentFreq(const std::string_view word) const;

    template <typename FilterFunction>
    std::vector<Document> FindAllDocuments(std::execution::sequenced_policy policy, const Query& query, FilterFunction filter_function) const;

    template <typename FilterFunction>
    std::vector<Document> FindAllDocuments(std::execution::parallel_policy policy, const Query& query, FilterFunction filter_function) const;
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words))
{
    using namespace std::string_literals;
    if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
        throw std::invalid_argument("Some of stop words are invalid"s);
    }
}

template <typename ExecutionPolicy, typename FilterFunction>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy& policy, const std::string_view raw_query, FilterFunction filter_function) const {
    if (!IsValidWord(raw_query)) {
        throw std::invalid_argument("Query contains invalid symbols");
    }
    const auto query = ParseQuerySorted(raw_query);
    auto matched_documents = FindAllDocuments(policy, query, filter_function);
    std::sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
        if (std::abs(lhs.relevance - rhs.relevance) < ACCURACY) {
            return lhs.rating > rhs.rating;
        }
        else {
            return lhs.relevance > rhs.relevance;
        }
        });
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}


template <typename FilterFunction>
std::vector<Document> SearchServer::FindTopDocuments(const std::string_view raw_query, FilterFunction filter_function) const {
    return FindTopDocuments(std::execution::seq, raw_query, filter_function);
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy& policy, const std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(policy, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy& policy, const std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <typename FilterFunction>
std::vector<Document> SearchServer::FindAllDocuments(std::execution::sequenced_policy policy, const Query& query, FilterFunction filter_function) const {
    std::map<int, double> document_to_relevance;
    for (const std::string_view word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
            const auto& document_data = documents_.at(document_id);
            if (filter_function(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id] += term_freq * inverse_document_freq;
            }
        }
    }
    for (const std::string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
            document_to_relevance.erase(document_id);
        }
    }
    std::vector<Document> matched_documents;
    for (const auto [document_id, relevance] : document_to_relevance) {
        matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
    }
    return matched_documents;
}

template <typename FilterFunction>
std::vector<Document> SearchServer::FindAllDocuments(std::execution::parallel_policy policy, const Query& query, FilterFunction filter_function) const {

    unsigned int threads_ = std::thread::hardware_concurrency();
    ConcurrentMap<int, double> document_to_relevance(threads_);

    std::for_each(policy, query.plus_words.begin(), query.plus_words.end(), [this, &document_to_relevance, &filter_function](const std::string_view word) {
        if (word_to_document_freqs_.count(word) == 0) {
            return;
        }
        const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
        for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
            const auto& document_data = documents_.at(document_id);
            if (filter_function(document_id, document_data.status, document_data.rating)) {
                document_to_relevance[document_id].ref_to_value += term_freq * inverse_document_freq;
            }
        }
        });

    std::for_each(policy, query.minus_words.begin(), query.minus_words.end(), [this, &document_to_relevance](const std::string_view word) {
        if (word_to_document_freqs_.count(word) == 0) {
            return;
        }
        for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
            document_to_relevance.Erase(document_id);
        }
        });

    std::map<int, double> result = document_to_relevance.BuildOrdinaryMap();
    std::vector<Document> matched_documents;
    for (const auto [document_id, relevance] : result) {
        matched_documents.push_back({ document_id, relevance, documents_.at(document_id).rating });
    }
    return matched_documents;
}
