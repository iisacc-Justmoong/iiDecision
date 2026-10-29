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

ExactDecisionMaker::ExactDecisionMaker(std::vector<JointState> states, std::shared_ptr<EvidenceMatcher> matcher)
    : states_(std::move(states)), matcher_(std::move(matcher))
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
    if (!matcher_) {
        static auto default_matcher = std::make_shared<EvidenceMatcher>();
        matcher_ = default_matcher;
    }
    schema_ = std::move(schema);
    categories_.resize(schema_.size());
    for (std::size_t c = 0; c < schema_.size(); ++c) {
        std::set<std::string> values;
        for (const auto& state : states_) values.insert(state.observations[c].value);
        if (values.size() > UINT32_MAX) throw std::length_error("too many categorical values");
        categories_[c].assign(values.begin(),values.end());
    }
    for (const auto& state : states_) for (std::size_t c = 0; c < schema_.size(); ++c)
        encoded_states_.push_back(static_cast<std::uint32_t>(std::lower_bound(
            categories_[c].begin(),categories_[c].end(),state.observations[c].value)-categories_[c].begin()));
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
    std::vector<std::size_t> columns;
    std::vector<std::uint32_t> query, values;
    for (const auto& [key, value] : observations) {
        const auto found = std::lower_bound(schema_.begin(),schema_.end(),key);
        if (found == schema_.end() || *found != key) throw std::invalid_argument("unknown observation key");
        const auto column = static_cast<std::size_t>(found-schema_.begin());
        const auto category = std::lower_bound(categories_[column].begin(),categories_[column].end(),value);
        if (category == categories_[column].end() || *category != value) throw std::invalid_argument("unknown observation value");
        columns.push_back(column);
        query.push_back(static_cast<std::uint32_t>(category-categories_[column].begin()));
    }
    for (std::size_t r = 0; r < states_.size(); ++r) for (const auto c : columns)
        values.push_back(encoded_states_[r*schema_.size()+c]);
    auto matching = matcher_->match(states_.size(),values,query);

    std::vector<long double> all_weights, matched_weights;
    std::vector<std::vector<long double>> candidate_weights(candidates.size());
    for (std::size_t row = 0; row < states_.size(); ++row) {
        const auto& state = states_[row];
        all_weights.push_back(state.log_weight);
        if (!matching.mask[row]) continue;
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
    report.execution = std::move(matching.execution);
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
