#pragma once

#include <iiDecision/LayaDecisionMaker.h>
#include <iiDecision/RagDecisionGate.h>

namespace iiDecision {

struct RagTextEvidence {
    std::string id;
    std::string document_id;
    std::size_t page = 1;
    std::string text;
};

struct LayaEvidenceTrace {
    std::string evidence_id;
    LayaAnswer relevance;
    LayaAnswer answerability;
};

struct LayaEvidenceReport {
    std::vector<RagEvidence> evidence;
    std::vector<LayaEvidenceTrace> traces;
};

// The maker must outlive the scorer. Each passage uses one local forward pass
// with two Noul questions. Image evidence requires a separate capable scorer.
class LayaEvidenceScorer final {
public:
    explicit LayaEvidenceScorer(const LayaDecisionMaker& maker) noexcept;
    LayaEvidenceScorer(LayaDecisionMaker&&) = delete;
    LayaEvidenceReport score(std::string_view query, std::span<const RagTextEvidence>) const;

private:
    const LayaDecisionMaker& maker_;
};

} // namespace iiDecision
