#include <iiDecision/ExactDecisionMaker.h>

#include <array>
#include <iomanip>
#include <iostream>
#include <utility>

int main(int argc,char** argv)
{
    try {
        std::vector<iiDecision::JointState> states;
        for (const auto* id : {"A", "B", "C"}) for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) {
            long double log_weight;
            if (!(std::cin >> log_weight)) return 2;
            states.push_back({id, {{"x", std::to_string(x)}, {"y", std::to_string(y)}}, log_weight});
        }
        int x, y;
        if (!(std::cin >> x >> y)) return 2;
        iiDecision::Context context;
        if (x >= 0) context.observations.push_back({"x", std::to_string(x)});
        if (y >= 0) context.observations.push_back({"y", std::to_string(y)});
        const std::array candidates{iiDecision::Candidate{"A"}, iiDecision::Candidate{"B"}, iiDecision::Candidate{"C"}};
        iiDecision::AccelerationOptions options;
        options.mode = iiDecision::ExecutionMode::CpuOnly;
        if (argc > 1) { options.mode = iiDecision::ExecutionMode::RequireAccelerators; options.backend_ids = {argv[1]}; }
        auto matcher = std::make_shared<iiDecision::EvidenceMatcher>(options);
        const auto report = iiDecision::ExactDecisionMaker(std::move(states),matcher).evaluate(context, candidates);
        std::cout << std::setprecision(std::numeric_limits<long double>::max_digits10)
                  << iiDecision::probability_mantissa_bits << ' ' << report.log_evidence_probability
                  << ' ' << report.entropy << ' ' << report.normalization_error
                  << ' ' << report.underflowed_candidates << ' ' << report.decision.candidate_id;
        for (const auto& p : report.probabilities) std::cout << ' ' << p.probability << ' ' << p.log_probability;
        std::cout << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
