#include <iiDecision/Evaluation.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

const std::array candidates{iiDecision::Candidate{"A"}, iiDecision::Candidate{"B"}};
constexpr auto negative_infinity = -std::numeric_limits<long double>::infinity();

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void near(long double actual, long double expected)
{
    const auto tolerance = 256 * std::numeric_limits<long double>::epsilon()
                         * std::max(1.L, std::abs(expected));
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, "numeric reference mismatch");
}

template<class Error, class Function>
void throws(Function function)
{
    try { function(); } catch (const Error&) { return; }
    throw std::runtime_error("expected exception was not thrown");
}

iiDecision::ExactDecisionMaker model()
{
    return iiDecision::ExactDecisionMaker({
        {"A", {{"signal", "yes"}}, std::log(18.L)},
        {"A", {{"signal", "no"}}, std::log(2.L)},
        {"B", {{"signal", "yes"}}, std::log(8.L)},
        {"B", {{"signal", "no"}}, std::log(72.L)},
    });
}

void prior()
{
    const auto report = model().evaluate({}, candidates);
    require(report.probabilities.size() == 2, "complete distribution is required");
    near(report.probabilities[0].probability, 1.L / 5);
    near(report.probabilities[1].probability, 4.L / 5);
    near(report.log_evidence_probability, 0);
    near(report.entropy, -0.2L * std::log(0.2L) - 0.8L * std::log(0.8L));
    require(report.decision.candidate_id == "B", "prior should select B");
}

void conditioning()
{
    auto implementation = model();
    const iiDecision::Context context{{{"signal", "yes"}}};
    const auto report = implementation.evaluate(context, candidates);
    require(report.probabilities.size() == 2, "complete posterior is required");
    near(report.probabilities[0].probability, 9.L / 13);
    near(report.probabilities[1].log_probability, std::log(4.L / 13));
    near(report.log_evidence_probability, std::log(26.L / 100));
    iiDecision::IDecisionMaker& maker = implementation;
    require(maker.decide(context, candidates).candidate_id == "A", "contract must use evidence");
    require(context.observations[0].value == "yes", "input must remain unchanged");
}

void correlation()
{
    iiDecision::ExactDecisionMaker maker({
        {"A", {{"x", "1"}, {"y", "1"}}, std::log(45.L)},
        {"A", {{"x", "0"}, {"y", "0"}}, std::log(45.L)},
        {"B", {{"x", "1"}, {"y", "0"}}, std::log(5.L)},
        {"B", {{"x", "0"}, {"y", "1"}}, std::log(5.L)},
    });
    const auto report = maker.evaluate({{{"x", "1"}, {"y", "1"}}}, candidates);
    require(report.probabilities.size() == 2, "correlated posterior is required");
    near(report.probabilities[0].probability, 1);
    near(report.probabilities[1].probability, 0);
    require(report.probabilities[1].log_probability == negative_infinity, "structural zero must remain zero");
    near(maker.evaluate({{{"x", "1"}}}, candidates).probabilities[0].probability, 0.9L);
}

void rareEvidence()
{
    iiDecision::ExactDecisionMaker maker({
        {"A", {{"signal", "common"}}, 0}, {"B", {{"signal", "common"}}, 0},
        {"A", {{"signal", "rare"}}, -10000},
        {"B", {{"signal", "rare"}}, -10000 + std::log(3.L)},
    });
    const auto report = maker.evaluate({{{"signal", "rare"}}}, candidates);
    require(report.probabilities.size() == 2, "rare evidence must be supported");
    // Input log(3)-10000 itself has finite-precision rounding.
    require(std::abs(report.probabilities[0].probability - 0.25L) < 1e-11L, "rare conditional mass lost");
    require(report.decision.candidate_id == "B", "rare evidence should select B");
    near(report.log_evidence_probability, -10000 + std::log(2.L));
}

long double underflowLog()
{
    return -2.L * std::numeric_limits<long double>::max_exponent * std::log(2.L);
}

void logRange()
{
    for (const auto offset : {10000.L, -10000.L}) {
        const auto report = iiDecision::ExactDecisionMaker({{"A", {}, offset}, {"B", {}, offset - 1}})
            .evaluate({}, candidates);
        require(report.probabilities.size() == 2, "shifted distribution is required");
        near(report.probabilities[0].probability, 1 / (1 + std::exp(-1.L)));
    }
    const auto report = iiDecision::ExactDecisionMaker({{"A", {}, 0}, {"B", {}, underflowLog()}})
        .evaluate({}, candidates);
    require(report.probabilities.size() == 2, "logarithmic tail must be preserved");
    near(report.probabilities[1].log_probability, underflowLog());
    require(report.probabilities[1].probability == 0 && report.underflowed_candidates == 1,
            "linear underflow must be reported");
}

void permutation()
{
    const auto first = model().evaluate({}, candidates);
    iiDecision::ExactDecisionMaker reordered({
        {"B", {{"signal", "no"}}, std::log(72.L)},
        {"B", {{"signal", "yes"}}, std::log(8.L)},
        {"A", {{"signal", "no"}}, std::log(2.L)},
        {"A", {{"signal", "yes"}}, std::log(18.L)},
    });
    const auto second = reordered.evaluate({}, candidates);
    require(first.probabilities.size() == 2 && second.probabilities.size() == 2, "distributions are required");
    require(first.probabilities[0].log_probability == second.probabilities[0].log_probability,
            "state order must not change the result");
    const std::array reversed{candidates[1], candidates[0]};
    const auto reverse = reordered.evaluate({}, reversed);
    require(reverse.probabilities[0].candidate_id == "B", "caller order must be retained");
    near(reverse.probabilities[0].probability, first.probabilities[1].probability);
}

void validation()
{
    using Maker = iiDecision::ExactDecisionMaker;
    throws<std::invalid_argument>([] { Maker maker({}); });
    for (auto weight : {negative_infinity, std::numeric_limits<long double>::infinity(),
                        std::numeric_limits<long double>::quiet_NaN()}) {
        throws<std::invalid_argument>([&] { Maker maker({{"A", {}, weight}}); });
    }
    throws<std::invalid_argument>([] { Maker maker({{"", {}, 0}}); });
    throws<std::invalid_argument>([] { Maker maker({{"A", {}, 0}, {"A", {}, 1}}); });
    throws<std::invalid_argument>([] { Maker maker({{"A", {{"x", "0"}, {"x", "1"}}, 0}}); });
    throws<std::invalid_argument>([] { Maker maker({{"A", {{"", "0"}}, 0}}); });
    throws<std::invalid_argument>([] { Maker maker({{"A", {{"x", "0"}}, 0}, {"B", {}, 0}}); });
    throws<std::overflow_error>([] {
        const auto limit = std::numeric_limits<long double>::max();
        Maker maker({{"A", {}, limit}, {"B", {}, -limit}});
    });
    auto maker = model();
    throws<std::invalid_argument>([&] { maker.evaluate({}, {}); });
    throws<std::invalid_argument>([&] { maker.evaluate({}, std::span(candidates).first(1)); });
    const std::array duplicate{candidates[0], candidates[0]};
    throws<std::invalid_argument>([&] { maker.evaluate({}, duplicate); });
    const std::array unknown{iiDecision::Candidate{"A"}, iiDecision::Candidate{"C"}};
    throws<std::invalid_argument>([&] { maker.evaluate({}, unknown); });
    throws<std::invalid_argument>([&] { maker.evaluate({{{"unknown", "yes"}}}, candidates); });
    throws<std::invalid_argument>([&] { maker.evaluate({{{"signal", "unknown"}}}, candidates); });
    throws<std::invalid_argument>([&] { maker.evaluate({{{"signal", "yes"}, {"signal", "no"}}}, candidates); });
}

void zeroMass()
{
    iiDecision::ExactDecisionMaker maker({
        {"A", {{"signal", "yes"}}, 0}, {"B", {{"signal", "no"}}, negative_infinity},
    });
    throws<std::domain_error>([&] { maker.evaluate({{{"signal", "no"}}}, candidates); });
    const auto report = maker.evaluate({}, candidates);
    require(report.probabilities.size() == 2, "zero-mass candidate must remain in output");
    near(report.probabilities[1].probability, 0);
}

void tie()
{
    iiDecision::ExactDecisionMaker maker({{"A", {}, 0}, {"B", {}, 0}});
    const std::array reversed{candidates[1], candidates[0]};
    require(maker.decide({}, reversed).candidate_id == "B", "ties must follow caller candidate order");
}

void evaluation()
{
    auto prediction = iiDecision::ExactDecisionMaker({{"A", {}, std::log(3.L)}, {"B", {}, 0}})
        .evaluate({}, candidates);
    const std::array cases{
        iiDecision::EvaluationCase{"right", prediction, "A"},
        iiDecision::EvaluationCase{"wrong", prediction, "B"},
    };
    auto report = iiDecision::evaluatePredictions(cases, 2);
    require(report.cases.size() == 2 && report.reliability.size() == 2, "case scores and bins required");
    near(report.accuracy, 0.5L);
    near(report.mean_log_loss, (-std::log(0.75L) - std::log(0.25L)) / 2);
    near(report.mean_brier_score, 0.625L);
    near(report.expected_calibration_error, 0.25L);
    require(report.reliability[1].count == 2 && report.reliability[0].count == 0, "fixed-width bins required");

    prediction = iiDecision::ExactDecisionMaker({{"A", {}, 0}, {"B", {}, underflowLog()}}).evaluate({}, candidates);
    auto tail = iiDecision::evaluatePredictions(std::array{iiDecision::EvaluationCase{"tail", prediction, "B"}});
    near(tail.mean_log_loss, -underflowLog());
    prediction = iiDecision::ExactDecisionMaker({{"A", {}, 0}, {"B", {}, negative_infinity}}).evaluate({}, candidates);
    auto impossible = iiDecision::evaluatePredictions(std::array{iiDecision::EvaluationCase{"zero", prediction, "B"}});
    require(std::isinf(impossible.mean_log_loss), "zero target must have infinite log loss");
    near(impossible.mean_brier_score, 2);
    require(impossible.reliability.back().count == 1, "p=1 must occupy the final bin");

    const auto extreme_loss = std::numeric_limits<long double>::max() * 0.6L;
    prediction = iiDecision::ExactDecisionMaker({{"A", {}, 0}, {"B", {}, -extreme_loss}}).evaluate({}, candidates);
    const auto extreme = iiDecision::evaluatePredictions(std::array{
        iiDecision::EvaluationCase{"extreme-1", prediction, "B"},
        iiDecision::EvaluationCase{"extreme-2", prediction, "B"},
    });
    near(extreme.mean_log_loss, extreme_loss);
}

void evaluationValidation()
{
    auto prediction = model().evaluate({}, candidates);
    throws<std::invalid_argument>([] { iiDecision::evaluatePredictions({}); });
    const std::array valid{iiDecision::EvaluationCase{"case", prediction, "A"}};
    throws<std::invalid_argument>([&] { iiDecision::evaluatePredictions(valid, 0); });
    const std::array duplicated{valid[0], valid[0]};
    throws<std::invalid_argument>([&] { iiDecision::evaluatePredictions(duplicated); });
    for (int invalid = 0; invalid < 8; ++invalid) {
        auto bad = valid;
        auto& p = bad[0].prediction;
        switch (invalid) {
        case 0: bad[0].expected_candidate_id = "unknown"; break;
        case 1: p.probabilities[0].probability = std::numeric_limits<long double>::quiet_NaN(); break;
        case 2: p.probabilities[0].probability = 0.4L; break;
        case 3: p.probabilities[0].log_probability = 1; break;
        case 4: p.probabilities[1].candidate_id = "A"; break;
        case 5: p.decision.candidate_id = "unknown"; break;
        case 6: bad[0].id.clear(); break;
        case 7:
            p.probabilities = {{"A", 1, 0}, {"B", 1e-30L, negative_infinity}};
            p.decision.candidate_id = "A";
            break;
        }
        throws<std::invalid_argument>([&] { iiDecision::evaluatePredictions(bad); });
    }
}

} // namespace

int main(int argc, char* argv[])
{
    const std::array tests{
        std::pair{"Prior", prior}, std::pair{"Conditioning", conditioning},
        std::pair{"Correlation", correlation}, std::pair{"RareEvidence", rareEvidence},
        std::pair{"LogRange", logRange}, std::pair{"Permutation", permutation},
        std::pair{"Validation", validation}, std::pair{"ZeroMass", zeroMass},
        std::pair{"Tie", tie}, std::pair{"Evaluation", evaluation},
        std::pair{"EvaluationValidation", evaluationValidation},
    };
    try {
        if (argc == 2) for (const auto& [name, run] : tests) {
            if (std::string_view(argv[1]) == name) { run(); return 0; }
        }
        throw std::runtime_error("unknown test name");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
