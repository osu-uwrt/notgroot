// Fuzzy matching for the finder: the query's characters in order, scored for word starts, runs and an early first
// match, case-insensitive (an uppercase query character must match uppercase), the matched positions kept for
// highlighting.
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace notgroot::core {
struct Match {
    bool matched = false;
    int score = 0;
    std::vector<int> positions; // matched character offsets in the text
};

Match fuzzyMatch(std::string_view query, std::string_view text);
} // namespace notgroot::core
