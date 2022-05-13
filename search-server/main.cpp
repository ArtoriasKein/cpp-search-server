#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <iostream>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const int MILLION = 1e-6;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result;
    cin >> result;
    ReadLine();
    return result;
}

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        }
        else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

struct Document {
    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    void AddDocument(int document_id, const string& document, DocumentStatus status, const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += inv_word_count;
        }
        documents_.emplace(document_id,
            DocumentData{
                ComputeAverageRating(ratings),
                status
            });
    }

    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(raw_query, [status](int, DocumentStatus doc_status, int) { return doc_status == status; });
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    template <typename PreFunc>
    vector<Document> FindTopDocuments(const string& raw_query, PreFunc pre_function) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, pre_function);
        sort(matched_documents.begin(), matched_documents.end(),
            [](const Document& lhs, const Document& rhs) {
                if (abs(lhs.relevance - rhs.relevance) < MILLION) {
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

    int GetDocumentCount() const {
        return static_cast<int>(documents_.size());
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query, int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return { matched_words, documents_.at(document_id).status };
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return {
            text,
            is_minus,
            IsStopWord(text)
        };
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                }
                else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }

    // Existence required
    double ComputeWordInverseDocumentFreq(const string& word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
    }

    template <typename PreFunc>
    vector<Document> FindAllDocuments(const Query& query, PreFunc pre_function) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double idf = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                if (pre_function(document_id, documents_.at(document_id).status, documents_.at(document_id).rating)) {
                    document_to_relevance[document_id] += term_freq * idf;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : document_to_relevance) {
            matched_documents.push_back({
                document_id,
                relevance,
                documents_.at(document_id).rating
                });
        }
        return matched_documents;
    }
};

void Assert(const bool& value, const string& value_str, const string& file, const string& func, unsigned line, const string& hint) {
    if (value == false) {
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT("s << value_str << ") failed."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

template <typename Value1, typename Value2>
void AssertEqual(const Value1& value1, const string& value1_str, const Value2& value2, const string& value2_str, const string& file, const string& func, unsigned line, const string& hint) {
    if (value1 != value2) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << value1_str << ", "s << value2_str << ") failed: "s;
        cerr << value1 << " != "s << value2 << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

template <typename Test>
void RunTestImpl(const Test& test, const string& test_name) {
    test();
    cerr << test_name << " OK" << endl;
}


#define ASSERT(value) Assert((value), #value, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_HINT(value, hint) Assert((value), #value, __FILE__, __FUNCTION__, __LINE__, (hint))
#define ASSERT_EQUAL(value1, value2) AssertEqual((value1), #value1, (value2), #value2, __FILE__, __FUNCTION__, __LINE__, ""s)
#define ASSERT_EQUAL_HINT(value1, value2, hint) AssertEqual((value1), #value1, (value2), #value2, __FILE__, __FUNCTION__, __LINE__, (hint))
#define RUN_TEST(func)  RunTestImpl((func), #func)

// -------- Начало модульных тестов поисковой системы ----------

// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов
void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = { 1, 2, 3 };
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(), "Stop words must be excluded from documents"s);
    }
}

//Тест на добавление и нахождение добавленного документа
void TestAddDocument() {
    SearchServer server;
    const int doc_id = 69;
    const string document = "funny monkey in the boat"s;
    const vector<int> ratings = { 1, 2, 3 };
    server.SetStopWords("in the"s);
    ASSERT(server.FindTopDocuments("monkey"s).empty());
    server.AddDocument(doc_id, document, DocumentStatus::ACTUAL, ratings);
    ASSERT_EQUAL(static_cast<int>(server.FindTopDocuments("monkey"s).size()), 1);
    ASSERT_EQUAL_HINT(server.FindTopDocuments("monkey"s)[0].id, doc_id, "Document id from server and initialized document id must match"s);
}

//Тест, проверяющий отсутствие документов, содержащих минус слова, в результате
void TestMinusWords() {
    SearchServer server;
    const int doc_id = 69;
    const string document = "funny monkey in the boat"s;
    const vector<int> ratings = { 1, 2, 3 };
    server.AddDocument(doc_id, document, DocumentStatus::ACTUAL, ratings);
    ASSERT_EQUAL(static_cast<int>(server.FindTopDocuments("monkey boat"s).size()), 1);
    ASSERT_HINT(server.FindTopDocuments("monkey -boat"s).empty(), "Results must be empty due to minus word"s);
}

//Тест, проверяющий правильную сортировку по релевантности у документов на выходе
void TestRelevanceSort() {
    SearchServer server;
    server.SetStopWords("и в на"s);
    server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });
    server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
    server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, { 5, -12, 2, 1 });
    server.AddDocument(3, "ухоженный скворец евгений"s, DocumentStatus::BANNED, { 9 });
    vector<double> result_for_test = { 0.866434, 0.173287, 0.173287 };
    vector<Document> result_from_server = server.FindTopDocuments("пушистый ухоженный кот"s);
    int i = 0;
    for (const auto& result : result_from_server) {
        ASSERT_EQUAL(round(result.relevance * 10000) / 10000, round(result_for_test[i] * 10000) / 10000);
        ++i;
    }
}

//Тест проверяющий возвращение слов из документа, соответствующих запросу
void TestMatchedDocuments() {
    SearchServer server;
    const int doc_id = 69;
    const string document = "funny monkey in the boat"s;
    const vector<int> ratings = { 1, 2, 3 };
    server.SetStopWords("in the"s);
    server.AddDocument(doc_id, document, DocumentStatus::ACTUAL, ratings);
    tuple<vector<string>, DocumentStatus> empty;
    ASSERT(server.MatchDocument("monkey -boat"s, 69) == empty);
    tuple<vector<string>, DocumentStatus> result = { { "boat"s, "monkey"s }, DocumentStatus::ACTUAL };
    tuple<vector<string>, DocumentStatus> compare = server.MatchDocument("monkey boat"s, 69);
    ASSERT(server.MatchDocument("monkey boat"s, 69) == result);
}

//Тест на вычисление рейтинга документов
void TestDocumentRating() {
    SearchServer server;
    server.SetStopWords("и в на"s);
    const vector<int> ratings = { 7, 2, 7 };
    server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, ratings);
    const int rating_for_test = (7 + 2 + 7) / 3;
    vector<Document> result_from_server = server.FindTopDocuments("пушистый ухоженный кот"s);
    ASSERT_EQUAL(result_from_server[0].rating, rating_for_test);

}

//Тест на фильтрацию результатов с помощью предиката
void TestPredicate() {
    SearchServer server;
    server.SetStopWords("и в на"s);
    server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });
    server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
    server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, { 5, -12, 2, 1 });
    server.AddDocument(3, "ухоженный скворец евгений"s, DocumentStatus::BANNED, { 9 });
    vector<Document> result_from_server = server.FindTopDocuments("пушистый ухоженный кот"s, [](int document_id, DocumentStatus, int) { return document_id % 2 == 0; });
    for (const auto& result : result_from_server) {
        ASSERT(result.id % 2 == 0);
    }
}

//Тест на поиск документа со статусом
void TestFindDocumentsWithStatus() {
    SearchServer server;
    server.SetStopWords("и в на"s);
    server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });
    server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
    server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL, { 5, -12, 2, 1 });
    server.AddDocument(3, "ухоженный скворец евгений"s, DocumentStatus::BANNED, { 9 });
    ASSERT(!server.FindTopDocuments("пушистый ухоженный кот"s, DocumentStatus::ACTUAL).empty());
    ASSERT(!server.FindTopDocuments("пушистый ухоженный кот"s, DocumentStatus::BANNED).empty());
    ASSERT(server.FindTopDocuments("пушистый ухоженный кот"s, DocumentStatus::IRRELEVANT).empty());
}


//Тест на корректное вычисление релевантности документов
void TestRelevance() {
    SearchServer server;
    server.AddDocument(0, "белый кот модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });
    vector<Document> result_from_server = server.FindTopDocuments("пушистый ухоженный кот"s);
    double relevance = log((server.GetDocumentCount() * 1.0) / 1) * (1.0 / 4.0);
    ASSERT_EQUAL(round(result_from_server[0].relevance * 10000) / 10000, round(relevance * 10000) / 10000);
}


// Функция TestSearchServer является точкой входа для запуска тестов
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestAddDocument);
    RUN_TEST(TestMinusWords);
    RUN_TEST(TestRelevanceSort);
    RUN_TEST(TestMatchedDocuments);
    RUN_TEST(TestDocumentRating);
    RUN_TEST(TestPredicate);
    RUN_TEST(TestFindDocumentsWithStatus);
    RUN_TEST(TestRelevance);
}

// --------- Окончание модульных тестов поисковой системы -----------

int main() {
    TestSearchServer();
    // Если вы видите эту строку, значит все тесты прошли успешно
    cout << "Search server testing finished"s << endl;
}