#include <iiDecision/LayaEvidenceScorer.h>

#include <array>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
const char* action(iiDecision::RagAction value)
{
    switch (value) {
    case iiDecision::RagAction::Answer: return "answer";
    case iiDecision::RagAction::RetrySearch: return "retry_search";
    case iiDecision::RagAction::Escalate: return "escalate";
    }
    throw std::logic_error("unknown RAG action");
}
}

int main(int argc, char** argv)
{
    try {
        using namespace iiDecision;
        std::cout << std::setprecision(std::numeric_limits<long double>::max_digits10);
        if (argc == 2) {
            LayaDecisionMaker maker(argv[1]);
            LayaEvidenceScorer scorer(maker);
            const std::array passages{
                RagTextEvidence{"refund-policy", "handbook", 17, "Refunds may be requested within 14 days of purchase."},
                RagTextEvidence{"appearance", "handbook", 3, "The application supports dark mode and custom themes."}
            };
            const auto scores = scorer.score("How long is the refund window?", passages);
            for (std::size_t i = 0; i < scores.evidence.size(); ++i) {
                const auto& evidence = scores.evidence[i];
                const auto& trace = scores.traces[i];
                std::cout << evidence.id << " relevance=" << evidence.relevance
                    << " answerability=" << evidence.answerability << " complete=" << evidence.complete
                    << " state_tokens_dropped=" << trace.answerability.state_tokens_dropped
                    << " head_tokens_dropped=" << trace.answerability.head_tokens_dropped
                    << " completed=" << trace.answerability.execution.records.at(0).completed << '\n';
            }
            const auto decision = RagDecisionGate{}.evaluate(scores.evidence);
            std::cout << "model_action=" << action(decision.action) << " pages=" << decision.pages.size() << '\n';
            return 0;
        }
        if (argc != 1) throw std::invalid_argument("usage: iiDecisionRagExample [local-laya-package]");
        RagPolicy policy;
        policy.revision = "support-example-v1";
        const auto result = RagDecisionGate(policy).run([](RagAttempt attempt) {
            if (attempt == RagAttempt::Initial) {
                return std::vector{RagEvidence{"related", "guide", 3, EvidenceModality::Text, 0.95L, 0.2L}};
            }
            // Scores from a separate image-capable model, illustrated with fixed values.
            return std::vector{RagEvidence{"error-diagram", "guide", 17, EvidenceModality::Image, 0.95L, 0.97L}};
        });
        for (const auto& report : result.attempts) {
            std::cout << "action=" << action(report.action) << " policy=" << report.policy.revision
                << " assessed=" << report.assessments.size() << '\n';
        }
        const auto& citation = result.attempts.back().pages.at(0);
        std::cout << "citation=" << citation.document_id << ':' << citation.page << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
