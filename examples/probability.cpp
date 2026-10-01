#include <iiDecision/Evaluation.h>

#include <array>
#include <cmath>
#include <iostream>

int main()
{
    iiDecision::ExactDecisionMaker maker({
        {"A", {{"signal", "yes"}}, std::log(18.L)},
        {"A", {{"signal", "no"}}, std::log(2.L)},
        {"B", {{"signal", "yes"}}, std::log(8.L)},
        {"B", {{"signal", "no"}}, std::log(72.L)},
    });
    const std::array candidates{iiDecision::Candidate{"A"}, iiDecision::Candidate{"B"}};
    const iiDecision::Context context{{{"signal", "yes"}}};
    const auto prediction = maker.evaluate(context, candidates);
    const std::array cases{iiDecision::EvaluationCase{"signal-yes", prediction, "A"}};
    const auto evaluation = iiDecision::evaluatePredictions(cases);
    std::cout << "selected=" << prediction.decision.candidate_id << '\n';
    for (const auto& p : prediction.probabilities) {
        std::cout << p.candidate_id << ": p=" << p.probability << ", log(p)=" << p.log_probability << '\n';
    }
    std::cout << "entropy=" << prediction.entropy
              << ", log_loss=" << evaluation.mean_log_loss
              << ", brier=" << evaluation.mean_brier_score << '\n';
}
