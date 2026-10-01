#include <iiDecision/DecisionModel.h>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace iiDecision;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance = 1e-12) {
    require(std::abs(actual - expected) <= tolerance, "Unexpected probability");
}

DecisionModel makeModel(ModelParameters parameters) {
    auto model = DecisionModel::create(std::move(parameters));
    require(model.has_value(), "Valid model was rejected");
    return std::move(*model);
}

template<class T>
void error(const std::expected<T, Error>& result, ErrorCode code) {
    require(!result.has_value(), "Invalid input was accepted");
    require(result.error().code == code, "Unexpected error code");
}

void probabilities() {
    auto model = makeModel({{{"first", "First", {1.0}, 0.0}, {"second", "Second", {0.0}, 0.0}}});
    const std::array state{std::log(3.0)};
    auto decision = model.predict(state);
    require(decision.has_value(), "Prediction failed");
    require(decision->choices.size() == 2, "Choices were lost");
    require(decision->choices[0].id == "first" && decision->choices[1].label == "Second", "Choice identity changed");
    near(decision->choices[0].probability, 0.75);
    near(decision->choices[1].probability, 0.25);
    near(decision->confidence, 0.75);
    near(decision->margin, 0.5);
    require(decision->selected_index == 0, "Wrong choice");
    near(decision->choices[0].probability + decision->choices[1].probability, 1.0);

    auto tempered = makeModel({model.parameters().options, 0.5});
    near(tempered.predict(state)->confidence, 0.9);
    auto extreme = makeModel({{{"high", "", {}, 1000.0}, {"low", "", {}, -1000.0}}});
    auto result = extreme.predict({});
    require(result.has_value(), "Stable softmax rejected finite logits");
    near(result->confidence, 1.0);
    near(result->choices[1].probability, 0.0);
    auto uniform = makeModel({{{"one", "", {}, 0.0}, {"two", "", {}, 0.0}, {"three", "", {}, 0.0}}});
    result = uniform.predict({});
    near(result->confidence, 1.0 / 3.0);
    near(result->margin, 0.0);
    require(result->selected_index == 0, "Tie must retain first configured option");
    auto single = makeModel({{{"only", "", {}, 0.0}}});
    near(single.predict({})->confidence, 1.0);
    near(single.predict({})->margin, 1.0);
}

void validation() {
    error(DecisionModel::create({}), ErrorCode::no_options);
    error(DecisionModel::create({{{"", "", {}, 0.0}}}), ErrorCode::invalid_option);
    error(DecisionModel::create({{{"same", "", {}, 0.0}, {"same", "", {}, 0.0}}}), ErrorCode::duplicate_option);
    error(DecisionModel::create({{{"a", "", {0.0}, 0.0}, {"b", "", {}, 0.0}}}), ErrorCode::feature_count_mismatch);
    for (double temperature : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        error(DecisionModel::create({{{"a", "", {}, 0.0}}, temperature}), ErrorCode::invalid_temperature);
    error(DecisionModel::create({{{"a", "", {std::numeric_limits<double>::infinity()}, 0.0}}}), ErrorCode::invalid_number);
    error(DecisionModel::create({{{"a", "", {}, std::numeric_limits<double>::quiet_NaN()}}}), ErrorCode::invalid_number);
    auto model = makeModel({{{"a", "", {1.0}, 0.0}}});
    error(model.predict({}), ErrorCode::feature_count_mismatch);
    std::array bad{std::numeric_limits<double>::quiet_NaN()};
    error(model.predict(bad), ErrorCode::invalid_number);
    auto overflow = makeModel({{{"a", "", {std::numeric_limits<double>::max()}, 0.0}}});
    const std::array two{2.0};
    error(overflow.predict(two), ErrorCode::arithmetic_overflow);
}

void training() {
    auto model = makeModel({{{"negative", "Negative", {0.0}, 0.0}, {"positive", "Positive", {0.0}, 0.0}}});
    const std::array examples{TrainingExample{{-2.0}, "negative"}, TrainingExample{{-1.0}, "negative"},
        TrainingExample{{1.0}, "positive"}, TrainingExample{{2.0}, "positive"}};
    auto report = model.train(examples, {200, 0.1});
    require(report.has_value(), "Training failed");
    require(report->updates == 800, "Wrong update count");
    require(report->mean_loss < 0.05, "Training did not learn the labeled decisions");
    const std::array positive{1.0}, negative{-1.0};
    auto p = model.predict(positive), n = model.predict(negative);
    require(p->selected_index == 1 && n->selected_index == 0, "Learned choices are wrong");
    require(p->confidence > 0.9 && n->confidence > 0.9, "Learning did not separate states");
    error(model.train({}), ErrorCode::no_examples);
    error(model.train(examples, {0, 0.1}), ErrorCode::invalid_training_options);
    error(model.train(examples, {1, 0.0}), ErrorCode::invalid_training_options);
    error(model.train(examples, {1, std::numeric_limits<double>::quiet_NaN()}), ErrorCode::invalid_training_options);
}

void atomic() {
    auto model = makeModel({{{"a", "", {0.0}, 0.0}, {"b", "", {0.0}, 0.0}}});
    const std::array examples{TrainingExample{{1.0}, "a"}, TrainingExample{{1.0}, "missing"}};
    error(model.train(examples), ErrorCode::unknown_option);
    near(model.parameters().options[0].weights[0], 0.0);
    const std::array mismatched{TrainingExample{{1.0, 2.0}, "a"}};
    error(model.train(mismatched), ErrorCode::feature_count_mismatch);
    const std::array invalid{TrainingExample{{std::numeric_limits<double>::infinity()}, "a"}};
    error(model.train(invalid), ErrorCode::invalid_number);
    const std::array tooLarge{TrainingExample{{std::numeric_limits<double>::max()}, "a"}};
    error(model.train(tooLarge, {1, std::numeric_limits<double>::max()}), ErrorCode::arithmetic_overflow);
    // The first sample updates the draft; the second overflows after those updates.
    const std::array laterOverflow{TrainingExample{{1.0}, "a"},
        TrainingExample{{std::numeric_limits<double>::max()}, "b"}};
    error(model.train(laterOverflow, {1, 2.0}), ErrorCode::arithmetic_overflow);
    near(model.parameters().options[0].weights[0], 0.0);
    near(model.parameters().options[0].bias, 0.0);
    const std::array state{1.0};
    near(model.predict(state)->confidence, 0.5);
}

void determinism() {
    auto model = makeModel({{{"a", "", {1.0}, 0.0}, {"b", "", {-1.0}, 0.0}}});
    auto independent = model;
    const std::array state{0.25};
    const auto expected = model.predict(state)->confidence;
    for (int i = 0; i < 100; ++i) near(model.predict(state)->confidence, expected, 0.0);
    const std::array examples{TrainingExample{{0.25}, "b"}};
    require(model.train(examples).has_value(), "Training failed");
    near(independent.predict(state)->confidence, expected, 0.0);
    require(model.predict(state)->selected_index == 1, "Instance did not learn separately");
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected test case name");
        const std::string_view name = argv[1];
        if (name == "probabilities") probabilities();
        else if (name == "validation") validation();
        else if (name == "training") training();
        else if (name == "atomic") atomic();
        else if (name == "determinism") determinism();
        else throw std::runtime_error("Unknown test case");
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
