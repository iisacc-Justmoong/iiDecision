#include <iiDecision/FirstCandidateDecisionMaker.h>
#include <array>
#include <iostream>
int main()
{
    const std::array candidates{iiDecision::Candidate{"A"}, iiDecision::Candidate{"B"}, iiDecision::Candidate{"C"}};
    iiDecision::FirstCandidateDecisionMaker implementation;
    iiDecision::IDecisionMaker& maker = implementation;
    std::cout << maker.decide(iiDecision::Context{}, candidates).candidate_id << '\n';
}
