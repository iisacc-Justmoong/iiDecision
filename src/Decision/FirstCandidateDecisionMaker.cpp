#include <iiDecision/FirstCandidateDecisionMaker.h>

#include <stdexcept>

namespace iiDecision {

Decision FirstCandidateDecisionMaker::decide(
    const Context&,
    std::span<const Candidate> candidates
) {
    if (candidates.empty()) {
        throw std::invalid_argument("iiDecision requires at least one candidate");
    }
    return Decision{candidates.front().id};
}

} // namespace iiDecision
