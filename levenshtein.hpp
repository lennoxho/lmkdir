#include <memory>
#include <string_view>
#include <locale>
#include <gsl/gsl>

#include "lmkdir_errors.hpp"

namespace DETAIL {
    
    template <typename CharType>
    inline bool char_ieq(CharType lhs, CharType rhs) {
        auto loc = std::locale();
        return std::tolower(lhs, loc) == std::tolower(rhs, loc);
    }
    
    inline bool char_ieq(char lhs, char rhs) noexcept {
        return tolower(lhs) == tolower(rhs);
    }
    
} // namespace DETAIL

struct LEVENSHTEIN_COST_TABLE {
    static constexpr std::size_t deletion = 1u;
    static constexpr std::size_t insertion = 1u;
    static constexpr std::size_t substitution = 1u;
};

template <typename CharType, bool CaseSensitive = true, typename CostTable = LEVENSHTEIN_COST_TABLE>
std::size_t levenshtein_distance(std::basic_string_view<CharType> lhs, std::basic_string_view<CharType> rhs, 
                                 gsl::span<std::size_t> working_buffer) 
{
    if (lhs.size() > rhs.size()) std::swap(lhs, rhs);
    RUNTIME_ASSERT(working_buffer.size() > lhs.size());

    const auto buffer = working_buffer.data();
    const auto buffer_end = buffer + lhs.size() + 1u;

    std::iota(buffer, buffer_end, 0u);

    std::size_t diag = 0u;
    for (std::size_t i = 0u; i < rhs.size(); ++i) { 
        diag = std::exchange(buffer[0u], i + 1u);

        for (std::size_t j = 0; j < lhs.size(); ++j) {
            if (CaseSensitive ? (lhs[j] == rhs[i]) : DETAIL::char_ieq(lhs[j], rhs[i])) {
                diag = std::exchange(buffer[j + 1u], diag);
            }
            else {
                /* UP   */ auto deletion_cost = buffer[j + 1u] + CostTable::deletion; 
                /* LEFT */ auto insertion_cost = buffer[j] + CostTable::insertion;
                /* DIAG */ auto substitution_cost = diag + CostTable::substitution;

                auto cost = std::min(deletion_cost, insertion_cost);
                cost = std::min(cost, substitution_cost);

                diag = std::exchange(buffer[j + 1u], cost);
            }
        }
    }

    return buffer[lhs.size()];
}

template <typename CharType, bool CaseSensitive = true, typename CostTable = LEVENSHTEIN_COST_TABLE>
inline std::size_t levenshtein_distance(std::basic_string_view<CharType> lhs, std::basic_string_view<CharType> rhs) {
    auto size = std::min(lhs.size(), rhs.size());
    auto buffer = std::make_unique<std::size_t[]>(size + 1);
    return levenshtein_distance<CharType, CaseSensitive, CostTable>(lhs, rhs, gsl::make_span(buffer.get(), size + 1));
}