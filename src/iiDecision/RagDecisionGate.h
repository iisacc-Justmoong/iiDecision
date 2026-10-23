#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace iiDecision {

enum class EvidenceModality { Text, Image };
enum class RagAttempt { Initial, Expanded };
enum class RagAction { Answer, RetrySearch, Escalate };
enum class RagRejection { LowRelevance, LowAnswerability, IncompleteEvidence, PageLimit };

struct RagEvidence {
    std::string id;
    std::string document_id;
    std::size_t page = 1; // One-based source page, scoped by document_id.
    EvidenceModality modality = EvidenceModality::Text;
    long double relevance = 0;
    long double answerability = 0;
    bool complete = true; // False when evidence or the scoring question was truncated.
};

struct RagThresholds {
    long double min_relevance = 0.7L;
    long double min_answerability = 0.8L;
};

struct RagPolicy {
    RagThresholds text;
    RagThresholds image;
    std::size_t max_pages = 5;
    std::string revision = "default";
};

struct RagAssessment {
    RagEvidence evidence;
    // Empty only for evidence included in the selected pages.
    std::vector<RagRejection> rejections;
};

struct RagPage {
    std::string document_id;
    std::size_t page = 1;
    // Accepted text/image evidence on this page, in caller order.
    std::vector<std::string> evidence_ids;
    std::string best_evidence_id;
};

struct RagDecisionReport {
    RagAction action = RagAction::Escalate;
    RagAttempt attempt = RagAttempt::Initial;
    RagPolicy policy;
    std::vector<RagAssessment> assessments; // All inputs, including rejections.
    std::vector<RagPage> pages; // Ranked unique citations, bounded by max_pages.
};

struct RagRunReport {
    // One or two owned reports. The last action is Answer or Escalate.
    std::vector<RagDecisionReport> attempts;
};

// A source retrieves AND scores candidates. Expanded must perform a wider search.
using RagEvidenceSource = std::function<std::vector<RagEvidence>(RagAttempt)>;

// Stateless policy over caller-provided judgments; no generation or network I/O.
class RagDecisionGate final {
public:
    explicit RagDecisionGate(RagPolicy policy = {});
    RagDecisionReport evaluate(std::span<const RagEvidence>,
        RagAttempt attempt = RagAttempt::Initial) const;
    // Calls the source once, or twice only if Initial requests RetrySearch.
    // Source exceptions propagate; they never become permission to answer.
    RagRunReport run(const RagEvidenceSource&) const;

private:
    RagPolicy policy_;
};

} // namespace iiDecision
