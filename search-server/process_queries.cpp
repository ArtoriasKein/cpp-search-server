#include "process_queries.h"

std::vector<std::vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> result(queries.size());
    std::transform(std::execution::par, queries.begin(), queries.end(), result.begin(), [&search_server](const std::string& query) {
        return search_server.FindTopDocuments(query);
        });
    return result;
}

std::list<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const std::vector<std::string>& queries) {
    std::vector<std::vector<Document>> pre_result(queries.size());
    std::transform(std::execution::par, queries.begin(), queries.end(), pre_result.begin(), [&search_server](const std::string& query) {
        return search_server.FindTopDocuments(query);
        });
    std::list<Document> result;
    for (const auto& documents : pre_result) {
        std::copy(documents.begin(), documents.end(), std::back_inserter(result));
    }
    return result;
}
