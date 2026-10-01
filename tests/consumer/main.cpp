#include <iiDecision/FirstCandidateDecisionMaker.h>

#include <array>

int main()
{
    const std::array candidates{iiDecision::Candidate{"A"}, iiDecision::Candidate{"B"}};
    iiDecision::FirstCandidateDecisionMaker implementation;
    iiDecision::IDecisionMaker& maker = implementation;
    const iiDecision::Decision decision = maker.decide(iiDecision::Context{}, candidates);
    return decision.candidate_id == "A" ? 0 : 1;
}
