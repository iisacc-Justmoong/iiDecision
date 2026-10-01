#pragma once

#include <iiDecision/IDecisionMaker.h>

namespace iiDecision {

class FirstCandidateDecisionMaker final : public IDecisionMaker {
public:
    // Throws std::invalid_argument when candidates is empty.
    Decision decide(
        const Context& context,
        std::span<const Candidate> candidates
    ) override;
};

} // namespace iiDecision
