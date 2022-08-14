#include "search_server.h"
#include <cmath>
#include <numeric>

using namespace std;

SearchServer::SearchServer(const string_view stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))
{
}

SearchServer::SearchServer(const string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))
{
}

void SearchServer::AddDocument(int document_id, const string_view document, DocumentStatus status, const vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    const vector<string_view> words = SplitIntoWordsNoStop(document);
    set<string> words_in_document;
    const double inv_word_count = 1.0 / words.size();
    for (const string_view& word : words) {
        words_in_document.insert(string(word));
        document_to_word_freqs_[document_id][*(words_in_document.find(string(word)))] += inv_word_count;
        word_to_document_freqs_[*(words_in_document.find(string(word)))][document_id] += inv_word_count;
    }
    documents_.emplace(document_id, DocumentData{ ComputeAverageRating(ratings), status, move(words_in_document) });
    document_ids_.insert(document_id);
}

int SearchServer::GetDocumentCount() const {
    return static_cast<int>(documents_.size());
}

SearchServer::it SearchServer::begin() {
    return document_ids_.begin();
}

SearchServer::it SearchServer::end() {
    return document_ids_.end();
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(const string_view raw_query, int document_id) const {
    return MatchDocument(execution::seq, raw_query, document_id);
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(execution::sequenced_policy policy, const string_view raw_query, int document_id) const {
    if (document_id < 0 || document_to_word_freqs_.count(document_id) == 0) {
        throw out_of_range("No document with such id"s);
    }

    const auto query = ParseQuerySorted(raw_query);
    for (const string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            return { {}, documents_.at(document_id).status };;
        }
    }

    vector<string_view> matched_words;
    for (const string_view word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            matched_words.push_back(*documents_.at(document_id).words_in_document.find(string(word)));
        }
    }
    return { matched_words, documents_.at(document_id).status };
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(execution::parallel_policy policy, const string_view raw_query, int document_id) const {
    if (document_id < 0 || document_to_word_freqs_.count(document_id) == 0) {
        throw out_of_range("No document with such id"s);
    }

    const auto query = ParseQuery(raw_query);
    if (any_of(policy, query.minus_words.begin(), query.minus_words.end(), [this, document_id](const string_view minus_word) {
        return word_to_document_freqs_.at(minus_word).count(document_id);
        })) {
        return { {}, documents_.at(document_id).status };
    }

    vector<string_view> matched_words;
    for_each(query.plus_words.begin(), query.plus_words.end(), [this, document_id, &matched_words](const string_view plus_word) {
        if (document_to_word_freqs_.at(document_id).count(plus_word)) {
            matched_words.push_back(*documents_.at(document_id).words_in_document.find(string(plus_word)));
        }
        });
    sort(policy, matched_words.begin(), matched_words.end());
    matched_words.erase(unique(policy, matched_words.begin(), matched_words.end()), matched_words.end());
    return { matched_words, documents_.at(document_id).status };
}

bool SearchServer::IsStopWord(string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(string_view word) {
    return none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
        });
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(const string_view text) const {
    vector<string_view> words;
    for (const string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = accumulate(ratings.begin(), ratings.end(), 0);
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(const string_view text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    string_view word = text;
    bool is_minus = false;
    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw invalid_argument("Query word is invalid");
    }
    return { word, is_minus, IsStopWord(word) };
}

SearchServer::Query SearchServer::ParseQuery(const string_view text) const {
    Query result;
    vector<string_view> words = SplitIntoWords(text);
    for (const string_view word : words) {

        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            }
            else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }
    return result;
}

SearchServer::Query SearchServer::ParseQuerySorted(const string_view text) const {
    Query result = ParseQuery(text);
    sort(result.plus_words.begin(), result.plus_words.end());
    sort(result.minus_words.begin(), result.minus_words.end());
    result.plus_words.erase(unique(result.plus_words.begin(), result.plus_words.end()), result.plus_words.end());
    result.minus_words.erase(unique(result.minus_words.begin(), result.minus_words.end()), result.minus_words.end());
    return result;
}

double SearchServer::ComputeWordInverseDocumentFreq(const string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.find(word)->second.size());
}

const map<string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static map<string_view, double> result;
    result.clear();
    for (const auto& [word, freqs] : document_to_word_freqs_.at(document_id)) {
        result[word] = freqs;
    }
    return result;
}

vector<Document> SearchServer::FindTopDocuments(const string_view raw_query) const {
    return FindTopDocuments(execution::seq, raw_query, DocumentStatus::ACTUAL);
}

vector<Document> SearchServer::FindTopDocuments(const string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(execution::seq, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}

void SearchServer::RemoveDocument(execution::sequenced_policy policy, int document_id) {
    if (documents_.count(document_id) == 0) {
        return;
    }
    for (const auto& [word, freq] : document_to_word_freqs_.at(document_id)) {
        word_to_document_freqs_[word].erase(document_id);
    }
    documents_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
    document_ids_.erase(document_id);
}

void SearchServer::RemoveDocument(int document_id) {
    return RemoveDocument(execution::seq, document_id);
}

void SearchServer::RemoveDocument(execution::parallel_policy policy, int document_id) {
    if (documents_.count(document_id) == 0) {
        return;
    }
    std::vector<const std::string*> words(document_to_word_freqs_.at(document_id).size());
    std::transform(policy, document_to_word_freqs_.at(document_id).begin(),
        document_to_word_freqs_.at(document_id).end(), words.begin(), [](auto& word) {
            return (&word.first);
        });
    std::for_each(policy, words.begin(), words.end(), [this, document_id](auto& word) {
        word_to_document_freqs_.find(*word)->second.erase(document_id);
        });

    documents_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
    document_ids_.erase(document_id);
}
