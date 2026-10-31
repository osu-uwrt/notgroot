// Fuzzy matching: best-scoring alignment of the query as a subsequence (dynamic programming, small inputs).
#include "notgroot/core/search.hpp"
#include <algorithm>
#include <cctype>
#include <limits>

namespace notgroot::core {
namespace {
bool isWordStart(std::string_view text, std::size_t i) {
    if (i == 0)
        return true;
    const unsigned char before = static_cast<unsigned char>(text[i - 1]), here = static_cast<unsigned char>(text[i]);
    if (!std::isalnum(before))
        return true;
    return std::islower(before) && std::isupper(here); // camelCase
}

bool same(char q, char t) {
    const unsigned char a = static_cast<unsigned char>(q), b = static_cast<unsigned char>(t);
    if (std::isupper(a))
        return a == b;
    return std::tolower(a) == std::tolower(b);
}
} // namespace

Match fuzzyMatch(std::string_view query, std::string_view text) {
    Match result;
    // Spaces in the query separate words that may match anywhere in order.
    std::string q;
    for (const char c : query)
        if (c != ' ')
            q += c;
    if (q.empty()) {
        result.matched = true;
        return result;
    }
    const std::size_t n = q.size(), m = text.size();
    if (n > m || m > 512)
        return result;

    // best[i][j]: best score with q[0..i] matched and q[i] at text[j]; from[i][j] where q[i-1] went.
    constexpr int none = std::numeric_limits<int>::min() / 2;
    std::vector<std::vector<int>> best(n, std::vector<int>(m, none)), from(n, std::vector<int>(m, -1));
    for (std::size_t j = 0; j < m; ++j)
        if (same(q[0], text[j]))
            best[0][j] = 10 + (isWordStart(text, j) ? 30 : 0) - int(std::min<std::size_t>(j, 15));
    for (std::size_t i = 1; i < n; ++i) {
        int running = none, runningAt = -1; // best of best[i-1][0..j-2]
        for (std::size_t j = i; j < m; ++j) {
            if (j >= 2 && best[i - 1][j - 2] > running) {
                running = best[i - 1][j - 2];
                runningAt = int(j - 2);
            }
            if (!same(q[i], text[j]))
                continue;
            const int bonus = 10 + (isWordStart(text, j) ? 30 : 0);
            if (best[i - 1][j - 1] > none && best[i - 1][j - 1] + bonus + 20 >= running + bonus - 3) {
                best[i][j] = best[i - 1][j - 1] + bonus + 20; // adjacent: a run
                from[i][j] = int(j - 1);
            } else if (running > none) {
                best[i][j] = running + bonus - 3;
                from[i][j] = runningAt;
            }
        }
    }
    int end = -1;
    for (std::size_t j = 0; j < m; ++j)
        if (best[n - 1][j] > none && (end < 0 || best[n - 1][j] > best[n - 1][std::size_t(end)]))
            end = int(j);
    if (end < 0)
        return result;
    result.matched = true;
    result.score = best[n - 1][std::size_t(end)] - int(m) / 4; // shorter texts first
    result.positions.resize(n);
    for (std::size_t i = n; i-- > 0;) {
        result.positions[i] = end;
        end = from[i][std::size_t(end)];
    }
    return result;
}
} // namespace notgroot::core
