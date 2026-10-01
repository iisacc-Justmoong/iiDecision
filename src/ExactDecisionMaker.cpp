#include <iiDecision/ExactDecisionMaker.h>

#include "Numerics.h"

#include <set>
#include <utility>

namespace iiDecision {

namespace {

using Assignment = std::vector<std::pair<std::string, std::string>>;

Assignment canonical(std::span<const Observation> observations)
{
    Assignment result;
    for (const auto& observation : observations) {
        if (observation.key.empty()) throw std::invalid_argument("observation keys must be nonempty");
        result.emplace_back(observation.key, observation.value);
    }
    std::sort(result.begin(), result.end());
    for (std::size_t i = 1; i < result.size(); ++i) {
        if (result[i - 1].first == result[i].first) {
            throw std::invalid_argument("each observation key must occur once");
        }
    }
    return result;
}

} // namespace

ExactDecisionMaker::ExactDecisionMaker(std::vector<JointState> states)
    : states_(std::move(states))
{
    if (states_.empty()) throw std::invalid_argument("a joint model needs states");
    std::set<std::pair<std::string, Assignment>> assignments;
    std::vector<std::string> schema;
    std::vector<long double> weights;
    for (auto& state : states_) {
        if (state.candidate_id.empty()) throw std::invalid_argument("model candidate IDs must be nonempty");
        if (!std::isfinite(state.log_weight) && state.log_weight != detail::negative_infinity) {
            throw std::invalid_argument("joint log weights must be finite or negative infinity");
        }
        const auto assignment = canonical(state.observations);
        if (!assignments.emplace(state.candidate_id, assignment).second) {
            throw std::invalid_argument("joint assignments must be unique");
        }
        std::vector<std::string> keys;
        state.observations.clear();
        for (const auto& [key, value] : assignment) {
            keys.push_back(key);
            state.observations.push_back({key, value});
        }
        if (weights.empty()) schema = keys;
        else if (keys != schema) throw std::invalid_argument("all joint states must use the same observation schema");
        weights.push_back(state.log_weight);
    }
    if (detail::logNormalizer(weights).maximum == detail::negative_infinity) {
        throw std::invalid_argument("joint model must have positive total mass");
    }
}

ProbabilityReport ExactDecisionMaker::evaluate(const Context& context, std::span<const Candidate> candidates) const
{
    if (states_.empty()) throw std::invalid_argument("joint model has no states");
    std::set<std::string> required, supplied;
    for (const auto& state : states_) required.insert(state.candidate_id);
    for (const auto& candidate : candidates) {
        if (!supplied.insert(candidate.id).second) throw std::invalid_argument("candidate IDs must be unique");
    }
    if (required != supplied) throw std::invalid_argument("candidates must equal the complete model candidate set");

    const auto observations = canonical(context.observations);
    for (const auto& [key, value] : observations) {
        bool known = false;
        for (const auto& state : states_) for (const auto& observation : state.observations) {
            if (observation.key == key && observation.value == value) known = true;
        }
        if (!known) throw std::invalid_argument("unknown observation key or value");
    }

    std::vector<long double> all_weights, matched_weights;
    std::vector<std::vector<long double>> candidate_weights(candidates.size());
    for (const auto& state : states_) {
        all_weights.push_back(state.log_weight);
        const bool matches = std::all_of(observations.begin(), observations.end(), [&](const auto& item) {
            return std::any_of(state.observations.begin(), state.observations.end(), [&](const auto& observation) {
                return observation.key == item.first && observation.value == item.second;
            });
        });
        if (!matches) continue;
        matched_weights.push_back(state.log_weight);
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].id == state.candidate_id) candidate_weights[i].push_back(state.log_weight);
        }
    }
    const auto matched = detail::logNormalizer(matched_weights);
    if (matched.maximum == detail::negative_infinity) {
        throw std::domain_error("observations have zero probability under the joint model");
    }

    ProbabilityReport report;
    report.log_evidence_probability = detail::relativeLogMass(matched, detail::logNormalizer(all_weights));
    detail::CompensatedSum mass, entropy;
    std::size_t selected = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto log_p = detail::relativeLogMass(detail::logNormalizer(candidate_weights[i]), matched);
        const auto probability = std::exp(log_p);
        report.probabilities.push_back({candidates[i].id, probability, log_p});
        mass.add(probability);
        if (probability > 0) entropy.add(-probability * log_p);
        else if (std::isfinite(log_p)) ++report.underflowed_candidates;
        if (log_p > report.probabilities[selected].log_probability) selected = i;
    }
    report.decision.candidate_id = candidates[selected].id;
    report.entropy = detail::finite(entropy.value());
    report.normalization_error = std::abs(mass.value() - 1);
    return report;
}

Decision ExactDecisionMaker::decide(const Context& context, std::span<const Candidate> candidates)
{
    return evaluate(context, candidates).decision;
}

} // namespace iiDecision
