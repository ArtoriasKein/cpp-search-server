#include "remove_duplicates.h"

using namespace std;

void RemoveDuplicates(SearchServer& search_server) {
	vector<int> duplicated_documents_ids;
	int i = 0;
	for (const int document_id : search_server) {
		++i;
		for (auto it = search_server.begin() + i; it < search_server.end(); ++it) {
			if (!(count(duplicated_documents_ids.begin(), duplicated_documents_ids.end(), *it)) && !(count(duplicated_documents_ids.begin(), duplicated_documents_ids.end(), document_id))) {
				map<string, double> result_1 = search_server.GetWordFrequencies(document_id);
				map<string, double> result_2 = search_server.GetWordFrequencies(*it);
				if (result_1.size() != result_2.size()) {
					goto exit;
				}
				for (const auto& [word, freq] : result_1){
					if (!(result_2.count(word))) {
						goto exit;
					}
				}
				cout << "Found duplicate document id "s << *it << endl;
				duplicated_documents_ids.push_back(*it);
				exit:;
			}

		}
	}
	for (int& id : duplicated_documents_ids) {
		search_server.RemoveDocument(id);
	}
}