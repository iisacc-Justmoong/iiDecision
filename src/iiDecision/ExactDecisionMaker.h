#pragma once

#include <iiDecision/IDecisionMaker.h>
#include <iiDecision/Acceleration.h>

#include <cstddef>
#include <limits>

namespace iiDecision {

struct JointState {
    std::string candidate_id;
    std::vector<Observation> observations;
    // Natural log of a nonnegative, unnormalized joint mass; -infinity means zero.
    long double log_weight = 0;
};

struct CandidateProbability {
    std::string candidate_id;
    long double probability;
    long double log_probability;
};

struct ProbabilityReport {
    Decision decision;
    std::vector<CandidateProbability> probabilities;
    long double log_evidence_probability = 0;
    long double entropy = 0; // Natural-log units (nats), not empirical accuracy.
    long double normalization_error = 0; // Absolute deviation of the probability sum from 1.
    std::size_t underflowed_candidates = 0;
    ExecutionReport execution; // Evidence matching only; probability reduction stays on the host.
};

inline constexpr int probability_mantissa_bits = std::numeric_limits<long double>::digits;

// Exhaustive finite-state inference. Exact refers to enumeration, not exact arithmetic.
class ExactDecisionMaker final : public IDecisionMaker {
public:
    explicit ExactDecisionMaker(std::vector<JointState> states,
        std::shared_ptr<EvidenceMatcher> matcher = {});

    // Candidates must contain the complete model candidate set, once each.
    // Invalid inputs throw invalid_argument; zero-mass evidence throws domain_error.
    ProbabilityReport evaluate(const Context&, std::span<const Candidate>) const;
    Decision decide(const Context&, std::span<const Candidate>) override;

private:
    std::vector<JointState> states_;
    std::shared_ptr<EvidenceMatcher> matcher_;
    std::vector<std::uint32_t> encoded_states_;
    std::vector<std::string> schema_;
    std::vector<std::vector<std::string>> categories_;
};

} // namespace iiDecision
