#pragma once

#include <iiDecision/ExactDecisionMaker.h>

namespace iiDecision {

struct EvaluationCase {
    std::string id;
    ProbabilityReport prediction;
    std::string expected_candidate_id;
};

struct CaseScore {
    std::string id;
    bool correct;
    long double selected_probability;
    long double log_loss;
    long double brier_score; // Unscaled multiclass sum of squared errors, range [0, 2].
};

struct ReliabilityBin {
    std::size_t count = 0;
    long double mean_selected_probability = 0;
    long double accuracy = 0;
};

struct EvaluationReport {
    std::vector<CaseScore> cases;
    long double accuracy = 0;
    long double mean_log_loss = 0;
    long double mean_brier_score = 0;
    long double expected_calibration_error = 0;
    std::vector<ReliabilityBin> reliability;
};

// Deterministic scoring of labeled, held-out predictions. Invalid inputs throw invalid_argument.
// A zero-probability target retains infinite log loss; no epsilon floor is applied.
EvaluationReport evaluatePredictions(std::span<const EvaluationCase>, std::size_t bins = 10);

} // namespace iiDecision
