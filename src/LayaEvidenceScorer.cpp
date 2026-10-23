#include <iiDecision/LayaEvidenceScorer.h>

#include <array>
#include <set>
#include <stdexcept>
#include <utility>

namespace iiDecision {
namespace {
bool blank(std::string_view value)
{
    return value.find_first_not_of(" \t\r\n\f\v") == std::string_view::npos;
}
// Query and passage remain separate JSON string values, including control bytes.
std::string quote(std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string output = "\"";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { output += '\\'; output += static_cast<char>(c); }
        else if (c < 0x20) { output += "\\u00"; output += hex[c >> 4]; output += hex[c & 15]; }
        else output += static_cast<char>(c);
    }
    output += '"';
    return output;
}
const std::array questions{
    LayaQuestion{"relevance", "Is the passage relevant to the query?", LayaQuestionType::Noul,
        {{"false", "The passage is unrelated to the query."}, {"true", "The passage concerns the query's subject."}}},
    LayaQuestion{"answerability", "Does the passage contain sufficient evidence to answer the query?", LayaQuestionType::Noul,
        {{"false", "Answering requires missing information or outside knowledge."},
         {"true", "The passage explicitly supports an answer to the query."}}}
};
}

LayaEvidenceScorer::LayaEvidenceScorer(const LayaDecisionMaker& maker) noexcept : maker_(maker) {}

LayaEvidenceReport LayaEvidenceScorer::score(std::string_view query, std::span<const RagTextEvidence> passages) const
{
    if (blank(query)) throw std::invalid_argument("RAG query must not be blank");
    std::set<std::string> ids;
    // Validate all identities and nonblank content before model inference.
    for (const auto& passage : passages) {
        if (blank(passage.id) || !ids.insert(passage.id).second || blank(passage.document_id)
            || passage.page == 0 || blank(passage.text)) {
            throw std::invalid_argument("RAG text needs a unique ID, document, one-based page and nonblank content");
        }
    }
    LayaEvidenceReport report;
    report.evidence.reserve(passages.size());
    report.traces.reserve(passages.size());
    for (const auto& passage : passages) {
        const auto state = "{\"query\":" + quote(query) + ",\"passage\":" + quote(passage.text) + "}";
        auto answers = maker_.predict(state, questions);
        const bool complete = answers[0].state_tokens_dropped == 0 && answers[1].state_tokens_dropped == 0
            && answers[0].head_tokens_dropped == 0 && answers[1].head_tokens_dropped == 0;
        report.evidence.push_back({passage.id, passage.document_id, passage.page, EvidenceModality::Text,
            answers[0].noul, answers[1].noul, complete});
        report.traces.push_back({passage.id, std::move(answers[0]), std::move(answers[1])});
    }
    return report;
}

} // namespace iiDecision
