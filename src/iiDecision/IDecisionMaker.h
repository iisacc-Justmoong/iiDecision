#pragma once

#include <span>
#include <string>

namespace iiDecision {

struct Candidate {
    std::string id;
};

struct Context {};

struct Decision {
    std::string candidate_id;
};

class IDecisionMaker {
public:
    virtual ~IDecisionMaker() = default;

    // Selects one of the supplied candidates. At least one candidate is required.
    virtual Decision decide(
        const Context& context,
        std::span<const Candidate> candidates
    ) = 0;
};

} // namespace iiDecision
