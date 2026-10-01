#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace iiDecision::detail {

inline constexpr auto negative_infinity = -std::numeric_limits<long double>::infinity();

class CompensatedSum {
public:
    void add(long double value)
    {
        const auto next = sum_ + value;
        correction_ += std::abs(sum_) >= std::abs(value)
            ? (sum_ - next) + value : (value - next) + sum_;
        sum_ = next;
    }
    long double value() const { return sum_ + correction_; }
private:
    long double sum_ = 0;
    long double correction_ = 0;
};

inline long double finite(long double value)
{
    if (!std::isfinite(value)) throw std::overflow_error("iiDecision arithmetic exceeds numeric range");
    return value;
}

struct LogNormalizer {
    long double maximum = negative_infinity;
    long double log_scaled_sum = 0;
};

inline LogNormalizer logNormalizer(std::span<const long double> input)
{
    std::vector<long double> logs;
    for (auto value : input) if (value != negative_infinity) logs.push_back(value);
    if (logs.empty()) return {};
    // Canonical reduction order also makes row permutations reproducible.
    std::sort(logs.begin(), logs.end(), std::greater<>{});
    CompensatedSum tail;
    for (std::size_t i = 1; i < logs.size(); ++i) {
        tail.add(std::exp(finite(logs[i] - logs[0])));
    }
    // Keep the leading 1 separate so a tiny tail survives in the logarithm.
    return {logs[0], finite(std::log1p(tail.value()))};
}

inline long double relativeLogMass(const LogNormalizer& part, const LogNormalizer& whole)
{
    if (part.maximum == negative_infinity) return negative_infinity;
    const auto result = finite(finite(part.maximum - whole.maximum)
                             + (part.log_scaled_sum - whole.log_scaled_sum));
    // A subset cannot exceed its total; remove a possible final rounding overshoot.
    return std::min(0.L, result);
}

} // namespace iiDecision::detail
