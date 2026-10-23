#pragma once

#include <iiDecision/ExactDecisionMaker.h>

#include <filesystem>
#include <string_view>

namespace iiDecision {

enum class LayaQuestionType { Choice = 0, Score = 1, Noul = 2 };

struct LayaOption {
    std::string id;
    std::string description;
};

struct LayaQuestion {
    std::string id;
    std::string instructions;
    LayaQuestionType type = LayaQuestionType::Choice;
    // Score IDs are "0", "1", ...; Noul IDs are "false", "true", in that order.
    std::vector<LayaOption> options;
};

struct LayaOptions {
    // Zero uses the checkpoint's token budgets.
    std::size_t max_length = 0;
    std::size_t head_max_length = 0;
    int intra_op_threads = 1;
    std::string instructions = "Select the best action for the current state.";
};

struct LayaAnswer {
    std::string question_id;
    LayaQuestionType type = LayaQuestionType::Choice;
    Decision decision;
    std::vector<CandidateProbability> probabilities;
    long double score = 0; // Expected level index for Score.
    long double noul = 0; // Probability of true for Noul.
    long double confidence = 0; // 1 - normalized entropy; Noul uses max(p).
    long double answer_confidence = 0; // max(p), distinct from empirical accuracy.
    long double act_probability = 0; // Upstream auxiliary head; not an execution gate.
    long double temperature = 1;
    std::size_t input_tokens = 0;
    std::size_t state_tokens = 0;
    std::size_t state_tokens_dropped = 0;
    std::size_t head_tokens_dropped = 0; // Instruction/option tokens removed by head and option limits.
    std::size_t options_distinct = 0; // After the upstream per-option token cap.
    ExecutionReport execution;
};

// Local ONNX inference using the official Laya sequence and decoding contracts.
// A package contains laya.onnx, rl_agent_config.json and tokenizer/{tokenizer,
// tokenizer_config}.json. Python is used only to prepare the package.
class LayaDecisionMaker final : public IDecisionMaker {
public:
    static bool available() noexcept;
    explicit LayaDecisionMaker(const std::filesystem::path& package, LayaOptions options = {});
    ~LayaDecisionMaker() override;
    LayaDecisionMaker(const LayaDecisionMaker&) = delete;
    LayaDecisionMaker& operator=(const LayaDecisionMaker&) = delete;

    // All questions for one UTF-8 state run together in one forward pass.
    // Results and distributions retain the caller's question and option order.
    std::vector<LayaAnswer> predict(std::string_view state, std::span<const LayaQuestion>) const;
    LayaAnswer evaluate(std::string_view state, std::span<const Candidate>) const;
    // Observation keys become a JSON object in insertion order; duplicates are rejected.
    Decision decide(const Context&, std::span<const Candidate>) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iiDecision
