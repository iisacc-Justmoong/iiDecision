#include <iiDecision/DecisionModel.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace iiDecision {
namespace {

std::unexpected<Error> fail(ErrorCode code, const char* message) {
    return std::unexpected(Error{code, message});
}

std::expected<void, Error> validateFeatures(std::span<const double> features, std::size_t count) {
    if (features.size() != count)
        return fail(ErrorCode::feature_count_mismatch, "State and model feature counts differ");
    if (!std::ranges::all_of(features, [](double value) { return std::isfinite(value); }))
        return fail(ErrorCode::invalid_number, "Features must be finite");
    return {};
}

} // namespace

DecisionModel::DecisionModel(ModelParameters parameters) : parameters_(std::move(parameters)) {}

std::size_t DecisionModel::featureCount() const noexcept {
    return parameters_.options.empty() ? 0 : parameters_.options.front().weights.size();
}

std::expected<DecisionModel, Error> DecisionModel::create(ModelParameters parameters) {
    if (parameters.options.empty())
        return fail(ErrorCode::no_options, "At least one option is required");
    if (!std::isfinite(parameters.temperature) || parameters.temperature <= 0.0)
        return fail(ErrorCode::invalid_temperature, "Temperature must be finite and positive");
    const auto count = parameters.options.front().weights.size();
    std::unordered_set<std::string> ids;
    for (const auto& option : parameters.options) {
        if (option.id.empty())
            return fail(ErrorCode::invalid_option, "Option IDs must not be empty");
        if (!ids.insert(option.id).second)
            return fail(ErrorCode::duplicate_option, "Option IDs must be unique");
        if (auto valid = validateFeatures(option.weights, count); !valid)
            return std::unexpected(valid.error());
        if (!std::isfinite(option.bias))
            return fail(ErrorCode::invalid_number, "Bias must be finite");
    }
    return DecisionModel(std::move(parameters));
}

std::expected<Decision, Error> DecisionModel::predict(std::span<const double> features) const {
    if (parameters_.options.empty())
        return fail(ErrorCode::no_options, "Model has no options");
    if (auto valid = validateFeatures(features, featureCount()); !valid)
        return std::unexpected(valid.error());

    std::vector<double> values;
    values.reserve(parameters_.options.size());
    for (const auto& option : parameters_.options) {
        double logit = option.bias;
        for (std::size_t i = 0; i < features.size(); ++i) {
            const auto product = option.weights[i] * features[i];
            logit += product;
            if (!std::isfinite(product) || !std::isfinite(logit))
                return fail(ErrorCode::arithmetic_overflow, "Linear prediction overflowed");
        }
        values.push_back(logit);
    }
    const auto maximum = *std::ranges::max_element(values);
    double total = 0.0;
    for (auto& value : values) {
        // A negative infinite difference/quotient means exp underflows to zero.
        value = std::exp((value - maximum) / parameters_.temperature);
        total += value;
    }
    if (!std::isfinite(total) || total <= 0.0)
        return fail(ErrorCode::arithmetic_overflow, "Probability normalization overflowed");

    Decision decision;
    decision.choices.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto probability = values[i] / total;
        const auto& option = parameters_.options[i];
        decision.choices.push_back({option.id, option.label, probability});
        if (probability > decision.confidence) {
            decision.selected_index = i;
            decision.confidence = probability;
        }
    }
    double second = 0.0;
    for (std::size_t i = 0; i < decision.choices.size(); ++i)
        if (i != decision.selected_index)
            second = std::max(second, decision.choices[i].probability);
    decision.margin = decision.confidence - second;
    return decision;
}

std::expected<TrainingReport, Error> DecisionModel::train(
    std::span<const TrainingExample> examples, TrainingOptions options) {
    if (parameters_.options.empty())
        return fail(ErrorCode::no_options, "Model has no options");
    if (examples.empty())
        return fail(ErrorCode::no_examples, "At least one training example is required");
    if (options.epochs == 0 || !std::isfinite(options.learning_rate) || options.learning_rate <= 0.0
        || options.epochs > std::numeric_limits<std::size_t>::max() / examples.size())
        return fail(ErrorCode::invalid_training_options, "Training options are outside the valid range");

    std::vector<std::size_t> labels;
    labels.reserve(examples.size());
    for (const auto& example : examples) {
        if (auto valid = validateFeatures(example.features, featureCount()); !valid)
            return std::unexpected(valid.error());
        const auto found = std::ranges::find(parameters_.options, example.option_id, &Option::id);
        if (found == parameters_.options.end())
            return fail(ErrorCode::unknown_option, "Training label is not a configured option");
        labels.push_back(static_cast<std::size_t>(found - parameters_.options.begin()));
    }

    // Keep the current instance unchanged until every update and loss check succeeds.
    auto draft = *this;
    TrainingReport report;
    for (std::size_t epoch = 0; epoch < options.epochs; ++epoch) {
        for (std::size_t sample = 0; sample < examples.size(); ++sample) {
            const auto& features = examples[sample].features;
            auto decision = draft.predict(features);
            if (!decision) return std::unexpected(decision.error());
            for (std::size_t option = 0; option < draft.parameters_.options.size(); ++option) {
                const auto target = option == labels[sample] ? 1.0 : 0.0;
                const auto gradient = (decision->choices[option].probability - target)
                    / draft.parameters_.temperature;
                const auto step = options.learning_rate * gradient;
                if (!std::isfinite(step))
                    return fail(ErrorCode::arithmetic_overflow, "Training gradient overflowed");
                auto& parameters = draft.parameters_.options[option];
                for (std::size_t feature = 0; feature < features.size(); ++feature) {
                    parameters.weights[feature] -= step * features[feature];
                    if (!std::isfinite(parameters.weights[feature]))
                        return fail(ErrorCode::arithmetic_overflow, "Training weight overflowed");
                }
                parameters.bias -= step;
                if (!std::isfinite(parameters.bias))
                    return fail(ErrorCode::arithmetic_overflow, "Training bias overflowed");
            }
            ++report.updates;
        }
    }
    for (std::size_t sample = 0; sample < examples.size(); ++sample) {
        auto decision = draft.predict(examples[sample].features);
        if (!decision) return std::unexpected(decision.error());
        const auto probability = std::max(decision->choices[labels[sample]].probability,
                                          std::numeric_limits<double>::min());
        report.mean_loss += -std::log(probability) / static_cast<double>(examples.size());
    }
    if (!std::isfinite(report.mean_loss))
        return fail(ErrorCode::arithmetic_overflow, "Training loss overflowed");
    parameters_ = std::move(draft.parameters_);
    return report;
}

} // namespace iiDecision
