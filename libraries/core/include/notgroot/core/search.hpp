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
// Typing a key after "{": what completes it at `cursor` (the rest of the first key, by name, that starts with what
// was typed, then "}" unless one follows), or "" when the cursor isn't in a key or nothing matches.
std::string keyCompletion(std::string_view text, std::size_t cursor, const std::vector<std::string> &keys);
} // namespace notgroot::core
