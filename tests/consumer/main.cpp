#include <iiDecision/FirstCandidateDecisionMaker.h>
#include <iiDecision/Evaluation.h>
#include <iiDecision/LayaDecisionMaker.h>
#include <iiDecision/LayaEvidenceScorer.h>

#include <array>

int main(int argc, char** argv)
{
    if (argc == 2) {
        iiDecision::LayaDecisionMaker laya(argv[1]);
        const std::array questions{iiDecision::LayaQuestion{"action","Choose",iiDecision::LayaQuestionType::Choice,{{"A",""},{"B",""}}}};
        const auto result = laya.predict("state", questions);
        if (result.size() != 1 || result[0].decision.candidate_id != "B") return 2;
        iiDecision::LayaDecisionMaker evidence_maker(argv[1], {.max_length=512, .head_max_length=192});
        const auto scored = iiDecision::LayaEvidenceScorer(evidence_maker).score("question",
            std::array{iiDecision::RagTextEvidence{"text", "guide", 17, "state"}});
        if (scored.evidence.size() != 1 || scored.traces.size() != 1 || !scored.evidence[0].complete
            || iiDecision::RagDecisionGate{}.evaluate(scored.evidence).action != iiDecision::RagAction::Answer) return 3;
    }
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
    const auto rag = iiDecision::RagDecisionGate{}.run([](iiDecision::RagAttempt attempt) {
        return attempt == iiDecision::RagAttempt::Initial ? std::vector<iiDecision::RagEvidence>{}
            : std::vector{iiDecision::RagEvidence{"page", "manual", 17, iiDecision::EvidenceModality::Image, 0.9L, 0.95L}};
    });
    if (rag.attempts.size() != 2 || rag.attempts.back().action != iiDecision::RagAction::Answer
        || rag.attempts.back().pages[0].page != 17) return 4;
    return posterior.probabilities.size() == 2 && evaluation.accuracy == 1 && !posterior.execution.records.empty() ? 0 : 1;
}
