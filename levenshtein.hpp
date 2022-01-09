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

struct LEVENSHTEIN_SCORE_TABLE {
    static constexpr std::int64_t deletion = -5;
    static constexpr std::int64_t insertion = -1;
    static constexpr std::int64_t substitution = -5;

    static constexpr std::int64_t match = 10;
    static constexpr std::int64_t first_match_bonus = 15;
    static constexpr std::int64_t consecutive_match = 15;
};

template <typename CharType, bool CaseSensitive = true, typename ScoreTable = LEVENSHTEIN_SCORE_TABLE>
std::int64_t modified_levenshtein_distance(std::basic_string_view<CharType> src, std::basic_string_view<CharType> tgt, 
                                           gsl::span<std::int64_t> working_buffer, gsl::span<std::byte> working_bitset) 
{
    RUNTIME_ASSERT(!src.empty());
    RUNTIME_ASSERT(!tgt.empty());
    
    auto deletion = ScoreTable::deletion;
    auto insertion = ScoreTable::insertion;
    if (src.size() > tgt.size()) {
        std::swap(src, tgt);
        std::swap(deletion, insertion);
    }
    RUNTIME_ASSERT(working_buffer.size() > src.size());
    RUNTIME_ASSERT(working_bitset.size() * CHAR_BIT >= src.size());

    const auto buffer = working_buffer.data();
    const auto buffer_size = src.size() + 1u;
    const auto bitset = working_bitset.data();

#if USE_SELLERS != 0
    std::memset(buffer, 0, buffer_size);
#else
    {
        std::int64_t n = 0;
        std::generate(buffer, buffer + buffer_size, [&n]{ return n--;});
    }
#endif
    std::memset(bitset, 0, (src.size() + CHAR_BIT - 1u) / CHAR_BIT);

    std::size_t num_matches = 0u;
    std::int64_t diag = 0;
    for (std::size_t i = 0u; i < tgt.size(); ++i) { 
        diag = std::exchange(buffer[0u], -static_cast<std::int64_t>(i + 1u));

        for (std::size_t j = 0u; j < src.size(); ++j) {
            const auto bitoffset = j / CHAR_BIT;
            const auto bitmask = std::byte(1u) << (j % CHAR_BIT);
            
            if (CaseSensitive ? (src[j] == tgt[i]) : DETAIL::char_ieq(src[j], tgt[i])) {
                auto score = diag + ScoreTable::match;
                
                if (j == 0u) score += ScoreTable::first_match_bonus;
                if ((bitset[bitoffset] & bitmask) != std::byte(0u)) score += ScoreTable::consecutive_match;

                diag = std::exchange(buffer[j + 1u], score);
                bitset[bitoffset] |= bitmask;
                
                ++num_matches;
            }
            else {
                /* UP   */ auto deletion_cost = buffer[j + 1u] + deletion;
                /* LEFT */ auto insertion_cost = buffer[j] + insertion;
                /* DIAG */ auto substitution_cost = diag + ScoreTable::substitution;

                auto score = std::max(insertion_cost, deletion_cost);
                score = std::max(score, substitution_cost);

                diag = std::exchange(buffer[j + 1u], score);
                bitset[bitoffset] &= ~bitmask;
            }
        }
    }

    return buffer[src.size()];
}

template <typename CharType, bool CaseSensitive = true, typename ScoreTable = LEVENSHTEIN_SCORE_TABLE>
inline std::int64_t modified_levenshtein_distance(std::basic_string_view<CharType> src, std::basic_string_view<CharType> tgt) {
    auto size = std::min(src.size(), tgt.size()) + 1u;
    auto buffer = std::make_unique<std::int64_t[]>(size);
    auto size_bytes = (size + CHAR_BIT - 1u) / CHAR_BIT;
    auto bitset = std::make_unique<std::byte[]>(size_bytes);

    return modified_levenshtein_distance<CharType, CaseSensitive, ScoreTable>(src, tgt, 
                                                                              gsl::make_span(buffer.get(), size), 
                                                                              gsl::make_span(bitset.get(), size_bytes));
}