#include <iiDecision/FirstCandidateDecisionMaker.h>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void selectsFirstCandidate()
{
    const iiDecision::Context context;
    const std::array candidates{
        iiDecision::Candidate{"A"},
        iiDecision::Candidate{"B"},
        iiDecision::Candidate{"C"},
    };
    iiDecision::FirstCandidateDecisionMaker implementation;
    iiDecision::IDecisionMaker& maker = implementation;

    const iiDecision::Decision decision = maker.decide(context, candidates);
    if (decision.candidate_id != "A") {
        throw std::runtime_error("A, B, C must select A; got '" + decision.candidate_id + "'");
    }
}

void rejectsEmptyCandidates()
{
    iiDecision::FirstCandidateDecisionMaker implementation;
    iiDecision::IDecisionMaker& maker = implementation;
    try {
        maker.decide(iiDecision::Context{}, {});
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("empty candidates must throw std::invalid_argument");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        if (argc != 2) {
            throw std::runtime_error("one test name is required");
        }
        const std::string_view name{argv[1]};
        if (name == "SelectsFirstCandidate") {
            selectsFirstCandidate();
        } else if (name == "RejectsEmptyCandidates") {
            rejectsEmptyCandidates();
        } else {
            throw std::runtime_error("unknown test name");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
