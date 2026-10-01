#include <iiDecision/DecisionModel.h>

#include <array>
#include <cmath>
#include <iostream>

int main() {
    auto model = iiDecision::DecisionModel::create({{
        {"accept", "Accept", {1.0}, 0.0}, {"reason", "Reason further", {0.0}, 0.0},
    }});
    if (!model) return 1;
    const std::array state{std::log(3.0)};
    auto decision = model->predict(state);
    if (!decision || decision->choices.size() != 2 || decision->selected_index != 0
        || std::abs(decision->confidence - 0.75) > 1e-12) return 1;
    const std::array examples{iiDecision::TrainingExample{{std::log(3.0)}, "reason"}};
    if (!model->train(examples) || model->predict(state)->selected_index != 1) return 1;
    std::cout << "installed decision model passed\n";
    return 0;
}
