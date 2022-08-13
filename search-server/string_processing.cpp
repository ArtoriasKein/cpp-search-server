#include "string_processing.h"
using namespace std;

vector<string_view> SplitIntoWords(string_view text) {
    vector<string_view> result;
    text.remove_prefix(min(text.find_first_not_of(" "), text.size()));
    while (!text.empty()) {
        size_t position_first_space = text.find(' ');
        string_view tmp_substr = text.substr(0, position_first_space);
        result.push_back(tmp_substr);
        text.remove_prefix(tmp_substr.size());
        text.remove_prefix(min(text.find_first_not_of(" "), text.size()));
    }
    return result;
}
