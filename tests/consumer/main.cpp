#include <iiDecision/FirstCandidateDecisionMaker.h>
#include <iiDecision/Evaluation.h>

#include <array>

int main()
{
    const std::array candidates{iiDecision::Candidate{"A"}, iiDecision::Candidate{"B"}};
    iiDecision::FirstCandidateDecisionMaker implementation;
    iiDecision::IDecisionMaker& maker = implementation;
    const iiDecision::Decision decision = maker.decide(iiDecision::Context{}, candidates);
    if (decision.candidate_id != "A") return 1;
    auto matcher = std::make_shared<iiDecision::EvidenceMatcher>();
    iiDecision::ExactDecisionMaker exact({{"A", {{"x","0"}}, 0}, {"B", {{"x","1"}}, 0}},matcher);
    const auto posterior = exact.evaluate(iiDecision::Context{{{"x","0"}}}, candidates);
    const std::array cases{iiDecision::EvaluationCase{"installed", posterior, "A"}};
    const auto evaluation = iiDecision::evaluatePredictions(cases);
    return posterior.probabilities.size() == 2 && evaluation.accuracy == 1 && !posterior.execution.records.empty() ? 0 : 1;
}
