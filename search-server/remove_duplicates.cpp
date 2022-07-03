#include "remove_duplicates.h"

using namespace std;

void RemoveDuplicates(SearchServer& search_server) {
	vector<int> duplicated_documents_ids;
	set<set<string>> all_document_words;
	for (const int document_id : search_server) {
		set<string> words_in_document;
		for (const auto& [word, freq] : search_server.GetWordFrequencies(document_id)) {
			words_in_document.insert(word);
		}
		if (all_document_words.contains(words_in_document)) {
			cout << "Found duplicate document id "s << document_id << endl;
			duplicated_documents_ids.push_back(document_id);
		}
		else {
			all_document_words.insert(words_in_document);
		}
	}
		for (int& id : duplicated_documents_ids) {
			search_server.RemoveDocument(id);
		}
}
