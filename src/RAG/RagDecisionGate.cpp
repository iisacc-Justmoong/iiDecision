#include <iiDecision/RagDecisionGate.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace iiDecision {
namespace {
bool blank(const std::string& value)
{
    return value.find_first_not_of(" \t\r\n\f\v") == std::string::npos;
}
void probability(long double value)
{
    if (!std::isfinite(value) || value < 0 || value > 1) {
        throw std::invalid_argument("RAG judgments and thresholds must be finite values in [0,1]");
    }
}
}

RagDecisionGate::RagDecisionGate(RagPolicy policy) : policy_(std::move(policy))
{
    probability(policy_.text.min_relevance);
    probability(policy_.text.min_answerability);
    probability(policy_.image.min_relevance);
    probability(policy_.image.min_answerability);
    if (policy_.max_pages == 0 || blank(policy_.revision)) {
        throw std::invalid_argument("RAG policy needs a positive page limit and a revision");
    }
}

RagDecisionReport RagDecisionGate::evaluate(std::span<const RagEvidence> evidence, RagAttempt attempt) const
{
    if (attempt != RagAttempt::Initial && attempt != RagAttempt::Expanded) {
        throw std::invalid_argument("unknown RAG retrieval attempt");
    }
    std::set<std::string> ids;
    for (const auto& item : evidence) {
        if (blank(item.id) || !ids.insert(item.id).second || blank(item.document_id) || item.page == 0) {
            throw std::invalid_argument("RAG evidence needs a unique ID, document ID and one-based page");
        }
        if (item.modality != EvidenceModality::Text && item.modality != EvidenceModality::Image) {
            throw std::invalid_argument("unknown RAG evidence modality");
        }
        probability(item.relevance);
        probability(item.answerability);
    }

    RagDecisionReport report;
    report.attempt = attempt;
    report.policy = policy_;
    report.assessments.reserve(evidence.size());
    std::vector<std::size_t> eligible;
    for (std::size_t i = 0; i < evidence.size(); ++i) {
        const auto& item = evidence[i];
        const auto& threshold = item.modality == EvidenceModality::Text ? policy_.text : policy_.image;
        RagAssessment assessment{item, {}};
        if (item.relevance < threshold.min_relevance) assessment.rejections.push_back(RagRejection::LowRelevance);
        if (item.answerability < threshold.min_answerability) assessment.rejections.push_back(RagRejection::LowAnswerability);
        if (!item.complete) assessment.rejections.push_back(RagRejection::IncompleteEvidence);
        if (assessment.rejections.empty()) eligible.push_back(i);
        report.assessments.push_back(std::move(assessment));
    }

    // Never combine one candidate's relevance with another's answerability.
    // Repeated evidence does not increase the probability or page rank.
    std::stable_sort(eligible.begin(), eligible.end(), [&](std::size_t a, std::size_t b) {
        if (evidence[a].answerability != evidence[b].answerability) {
            return evidence[a].answerability > evidence[b].answerability;
        }
        return evidence[a].relevance > evidence[b].relevance;
    });
    using PageKey = std::pair<std::string, std::size_t>;
    std::map<PageKey, std::size_t> selected;
    for (const auto index : eligible) {
        const auto& item = evidence[index];
        const PageKey key{item.document_id, item.page};
        if (selected.contains(key) || report.pages.size() == policy_.max_pages) continue;
        selected.emplace(key, report.pages.size());
        report.pages.push_back({item.document_id, item.page, {}, item.id});
    }
    // Preserve input order inside each selected page and in the audit trail.
    for (auto& assessment : report.assessments) {
        if (!assessment.rejections.empty()) continue;
        const auto& item = assessment.evidence;
        const auto page = selected.find({item.document_id, item.page});
        if (page == selected.end()) assessment.rejections.push_back(RagRejection::PageLimit);
        else report.pages[page->second].evidence_ids.push_back(item.id);
    }
    report.action = !report.pages.empty() ? RagAction::Answer
        : attempt == RagAttempt::Initial ? RagAction::RetrySearch : RagAction::Escalate;
    return report;
}

RagRunReport RagDecisionGate::run(const RagEvidenceSource& source) const
{
    if (!source) throw std::invalid_argument("RAG workflow needs a retrieval and scoring source");
    RagRunReport report;
    report.attempts.push_back(evaluate(source(RagAttempt::Initial), RagAttempt::Initial));
    if (report.attempts.back().action == RagAction::RetrySearch) {
        report.attempts.push_back(evaluate(source(RagAttempt::Expanded), RagAttempt::Expanded));
    }
    return report;
}

} // namespace iiDecision
