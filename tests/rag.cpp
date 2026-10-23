#include <iiDecision/RagDecisionGate.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace iiDecision;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class E, class F> void rejects(F f) {
    try { f(); } catch (const E&) { return; }
    throw std::runtime_error("expected exception");
}
RagEvidence text(std::string id = "text", std::string document = "guide", std::size_t page = 17,
    long double relevance = 0.9L, long double answerability = 0.9L)
{
    return {std::move(id), std::move(document), page, EvidenceModality::Text, relevance, answerability};
}
bool has(const RagAssessment& assessment, RagRejection reason) {
    return std::find(assessment.rejections.begin(), assessment.rejections.end(), reason) != assessment.rejections.end();
}
void answerability() {
    RagDecisionGate gate;
    const std::array evidence{text("related", "manual", 3, 1, 0.2L), text("answer", "guide", 17)};
    const auto report = gate.evaluate(evidence);
    check(report.action == RagAction::Answer && report.pages.size() == 1, "sufficient evidence permits an answer");
    check(report.pages[0].document_id == "guide" && report.pages[0].page == 17, "citation retains document and page");
    check(report.assessments.size() == 2 && has(report.assessments[0], RagRejection::LowAnswerability),
        "high relevance alone does not support an answer");
    check(report.assessments[1].rejections.empty(), "selected evidence is accepted");
    const auto irrelevant = gate.evaluate(std::array{text("wrong-topic", "guide", 17, 0.1L, 1)});
    check(irrelevant.action == RagAction::RetrySearch && has(irrelevant.assessments[0], RagRejection::LowRelevance),
        "high answerability does not bypass relevance");
}
void multimodal() {
    RagPolicy policy;
    policy.text = {0.8L, 0.85L}; policy.image = {0.6L, 0.7L};
    RagDecisionGate gate(policy);
    auto image = text("diagram", "guide", 17, 0.7L, 0.8L); image.modality = EvidenceModality::Image;
    auto cross = text("partial-text", "guide", 17, 1, 0.1L);
    auto cross_image = image; cross_image.id = "partial-image"; cross_image.relevance = 0.1L; cross_image.answerability = 1;
    const auto rejected = gate.evaluate(std::array{cross, cross_image});
    check(rejected.action == RagAction::RetrySearch && rejected.pages.empty(),
        "different failing candidates must not supply each other's missing score");
    const auto report = gate.evaluate(std::array{text("body"), image, text("same-page"), text("other-doc", "other", 17)});
    check(report.pages.size() == 2 && report.pages[0].evidence_ids == std::vector<std::string>{"body", "diagram", "same-page"},
        "text and image share one citation without merging different documents");
    check(report.pages[1].document_id == "other" && report.policy.image.min_answerability == 0.7L,
        "separate modality thresholds and policy snapshot");
}
void ranking() {
    RagPolicy policy; policy.max_pages = 2;
    RagDecisionGate gate(policy);
    const std::array evidence{text("retrieval-first", "manual", 1, 1, 0.81L),
        text("strong", "manual", 2, 0.8L, 0.99L), text("stronger-relevance", "manual", 3, 0.9L, 0.99L),
        text("duplicate-page", "manual", 2, 0.99L, 0.98L)};
    const auto report = gate.evaluate(evidence);
    check(report.pages.size() == 2 && report.pages[0].page == 3 && report.pages[1].page == 2,
        "rank by answerability then relevance before limiting pages");
    check(report.pages[1].evidence_ids.size() == 2 && report.pages[1].best_evidence_id == "strong",
        "duplicates do not consume the page budget or inflate probabilities");
    check(has(report.assessments[0], RagRejection::PageLimit), "capped evidence remains in audit report");
    const auto tie = gate.evaluate(std::array{text("first", "doc", 9), text("second", "doc", 2)});
    check(tie.pages[0].page == 9, "score ties retain input order");
    auto permuted = evidence; std::reverse(permuted.begin(), permuted.end());
    check(gate.evaluate(permuted).pages[0].page == 3, "unequal scores ignore retrieval ordering");
}
void boundaries() {
    RagDecisionGate gate;
    const auto at = text("at", "guide", 1, 0.7L, 0.8L);
    const auto below = text("below", "guide", 2, std::nextafter(0.7L, 0.L), std::nextafter(0.8L, 0.L));
    const auto report = gate.evaluate(std::array{at, below});
    check(report.pages.size() == 1 && report.pages[0].page == 1, "inclusive thresholds without rounding or epsilon");
    check(report.assessments[1].rejections.size() == 2, "both failed comparisons remain visible");
    auto truncated = text(); truncated.complete = false;
    const auto incomplete = gate.evaluate(std::array{truncated});
    check(incomplete.action == RagAction::RetrySearch && has(incomplete.assessments[0], RagRejection::IncompleteEvidence),
        "confident truncated input cannot authorize generation");
    RagPolicy zero; zero.text = {0, 0};
    check(RagDecisionGate(zero).evaluate(std::array{text("zero", "doc", 1, 0, 0)}).action == RagAction::Answer,
        "caller can explicitly choose boundary thresholds");
}
void workflow() {
    RagPolicy policy; policy.revision = "support-v2";
    RagDecisionGate gate(policy);
    std::vector<RagAttempt> calls;
    const auto result = gate.run([&](RagAttempt attempt) {
        calls.push_back(attempt);
        return attempt == RagAttempt::Initial ? std::vector{text("related", "doc", 1, 1, 0.2L)} : std::vector{text("expanded")};
    });
    check(calls == std::vector{RagAttempt::Initial, RagAttempt::Expanded} && result.attempts.size() == 2,
        "insufficient initial evidence triggers exactly one wider retrieval");
    check(result.attempts[0].action == RagAction::RetrySearch && result.attempts[1].action == RagAction::Answer,
        "expanded evidence can authorize the answer");
    check(result.attempts[0].assessments[0].evidence.id == "related" && result.attempts[1].policy.revision == "support-v2",
        "audit retains rejected initial evidence and policy revision");
    calls.clear();
    const auto escalation = gate.run([&](RagAttempt attempt) { calls.push_back(attempt); return std::vector<RagEvidence>{}; });
    check(calls.size() == 2 && escalation.attempts.back().action == RagAction::Escalate, "empty expanded search escalates");
    calls.clear();
    const auto direct = gate.run([&](RagAttempt attempt) { calls.push_back(attempt); return std::vector{text()}; });
    check(calls.size() == 1 && direct.attempts.size() == 1 && direct.attempts[0].action == RagAction::Answer,
        "sufficient initial evidence avoids retry");
    check(gate.evaluate({}, RagAttempt::Expanded).action == RagAction::Escalate, "expanded search cannot request another retry");
    rejects<std::invalid_argument>([&] { gate.run({}); });
    rejects<std::runtime_error>([&] { gate.run([](RagAttempt) -> std::vector<RagEvidence> { throw std::runtime_error("retrieval failed"); }); });
    std::size_t failed_calls = 0;
    rejects<std::runtime_error>([&] { gate.run([&](RagAttempt) -> std::vector<RagEvidence> {
        if (++failed_calls == 2) throw std::runtime_error("expanded scoring failed");
        return {};
    }); });
    check(failed_calls == 2, "failed expanded scoring never authorizes an answer or loops");
}
void validation() {
    RagDecisionGate gate;
    auto bad = text(); bad.id.clear(); rejects<std::invalid_argument>([&] { gate.evaluate(std::array{bad}); });
    bad = text(); bad.document_id = " \t"; rejects<std::invalid_argument>([&] { gate.evaluate(std::array{bad}); });
    bad = text(); bad.page = 0; rejects<std::invalid_argument>([&] { gate.evaluate(std::array{bad}); });
    bad = text(); bad.modality = static_cast<EvidenceModality>(42); rejects<std::invalid_argument>([&] { gate.evaluate(std::array{bad}); });
    rejects<std::invalid_argument>([&] { gate.evaluate(std::array{text(), text()}); });
    rejects<std::invalid_argument>([&] { gate.evaluate({}, static_cast<RagAttempt>(42)); });
    for (const auto number : {-0.01L, 1.01L, std::numeric_limits<long double>::quiet_NaN(),
        std::numeric_limits<long double>::infinity()}) {
        bad = text(); bad.relevance = number; rejects<std::invalid_argument>([&] { gate.evaluate(std::array{bad}); });
        bad = text(); bad.answerability = number; rejects<std::invalid_argument>([&] { gate.evaluate(std::array{bad}); });
        for (int field = 0; field < 4; ++field) {
            RagPolicy policy;
            const std::array fields{&policy.text.min_relevance, &policy.text.min_answerability,
                &policy.image.min_relevance, &policy.image.min_answerability};
            *fields[field] = number; rejects<std::invalid_argument>([&] { RagDecisionGate invalid(policy); });
        }
    }
    RagPolicy policy; policy.max_pages = 0; rejects<std::invalid_argument>([&] { RagDecisionGate invalid(policy); });
    policy = {}; policy.revision = "\n"; rejects<std::invalid_argument>([&] { RagDecisionGate invalid(policy); });
}
void ownership() {
    RagPolicy policy; policy.revision = "original";
    const RagDecisionGate gate(policy); policy.revision = "modified";
    auto evidence = std::vector{text()};
    const auto report = gate.evaluate(evidence);
    evidence[0].id = "modified"; evidence.clear();
    check(report.policy.revision == "original" && report.assessments[0].evidence.id == "text", "reports own their inputs");
    std::vector<std::future<void>> jobs;
    for (int i = 0; i < 4; ++i) jobs.push_back(std::async(std::launch::async, [&] {
        for (int j = 0; j < 30; ++j) check(gate.evaluate(std::array{text()}).action == RagAction::Answer, "concurrent gate evaluations");
    }));
    for (auto& job : jobs) job.get();
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "one test case required"); const std::string_view name = argv[1];
        if (name == "Answerability") answerability();
        else if (name == "Multimodal") multimodal();
        else if (name == "Ranking") ranking();
        else if (name == "Boundaries") boundaries();
        else if (name == "Workflow") workflow();
        else if (name == "Validation") validation();
        else if (name == "Ownership") ownership();
        else throw std::runtime_error("unknown test");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
