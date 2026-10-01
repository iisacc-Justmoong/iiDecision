#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace iiDecision {

enum class ErrorCode {
    no_options,
    invalid_option,
    duplicate_option,
    feature_count_mismatch,
    invalid_temperature,
    invalid_number,
    arithmetic_overflow,
    no_examples,
    unknown_option,
    invalid_training_options,
};

struct Error {
    ErrorCode code;
    std::string message;
};

struct Option {
    std::string id;
    std::string label;
    std::vector<double> weights;
    double bias = 0.0;
};

struct ModelParameters {
    std::vector<Option> options;
    double temperature = 1.0;
};

struct ChoiceProbability {
    std::string id;
    std::string label;
    double probability = 0.0;
};

struct Decision {
    std::vector<ChoiceProbability> choices;
    std::size_t selected_index = 0;
    double confidence = 0.0; // Selected probability; not a calibration guarantee.
    double margin = 0.0;     // Largest probability minus second largest.
};

struct TrainingExample {
    std::vector<double> features;
    std::string option_id;
};

struct TrainingOptions {
    std::size_t epochs = 100;
    double learning_rate = 0.1;
};

struct TrainingReport {
    std::size_t updates = 0;
    double mean_loss = 0.0;
};

class DecisionModel final {
public:
    [[nodiscard]] static std::expected<DecisionModel, Error> create(ModelParameters parameters);
    [[nodiscard]] std::expected<Decision, Error> predict(std::span<const double> features) const;
    [[nodiscard]] std::expected<TrainingReport, Error> train(
        std::span<const TrainingExample> examples, TrainingOptions options = {});
    [[nodiscard]] const ModelParameters& parameters() const noexcept { return parameters_; }
    [[nodiscard]] std::size_t featureCount() const noexcept;

private:
    explicit DecisionModel(ModelParameters parameters);
    ModelParameters parameters_;
};

} // namespace iiDecision
