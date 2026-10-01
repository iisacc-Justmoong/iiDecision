#include <iiDecision/DecisionModel.h>

#include <array>
#include <iostream>

int main() {
    auto model = iiDecision::DecisionModel::create({{
        {"local", "Use the local workflow", {-4.0, -4.0}, 4.0},
        {"llm", "Use a language model", {4.0, 4.0}, -4.0},
    }});
    if (!model) return 1;
    // Application-defined, normalized features: complexity and novelty.
    const std::array state{0.1, 0.2};
    const auto decision = model->predict(state);
    if (!decision) return 1;
    for (const auto& choice : decision->choices)
        std::cout << choice.id << '=' << choice.probability << '\n';
    std::cout << "selected=" << decision->choices[decision->selected_index].id
              << " confidence=" << decision->confidence << " margin=" << decision->margin << '\n';
    // The caller can use these values before deciding whether to invoke an LLM.
    return 0;
}
