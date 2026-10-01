#include <iiDecision/Evaluation.h>

#include "Numerics.h"

#include <set>

namespace iiDecision {

EvaluationReport evaluatePredictions(std::span<const EvaluationCase> cases, std::size_t bins)
{
    if (cases.empty() || bins == 0) throw std::invalid_argument("evaluation requires cases and reliability bins");
    EvaluationReport report;
    report.reliability.resize(bins);
    std::vector<detail::CompensatedSum> bin_probabilities(bins), bin_correct(bins);
    detail::CompensatedSum correct, log_loss, brier;
    bool infinite_loss = false;
    std::set<std::string> case_ids;
    const auto count = static_cast<long double>(cases.size());

    for (const auto& sample : cases) {
        if (sample.id.empty() || !case_ids.insert(sample.id).second) {
            throw std::invalid_argument("evaluation case IDs must be nonempty and unique");
        }
        const auto& prediction = sample.prediction;
        if (prediction.probabilities.empty()) throw std::invalid_argument("predictions require a distribution");
        const auto tolerance = 64 * std::numeric_limits<long double>::epsilon()
                             * static_cast<long double>(prediction.probabilities.size());
        detail::CompensatedSum mass, sample_brier;
        std::set<std::string> ids;
        bool target_found = false, selected_found = false;
        long double target_log_p = 0, selected_p = 0;
        for (const auto& p : prediction.probabilities) {
            if (p.candidate_id.empty() || !ids.insert(p.candidate_id).second) {
                throw std::invalid_argument("prediction candidate IDs must be nonempty and unique");
            }
            if (!std::isfinite(p.probability) || p.probability < 0 || p.probability > 1
                || ((!std::isfinite(p.log_probability)) && p.log_probability != detail::negative_infinity)
                || p.log_probability > 0
                || std::abs(std::exp(p.log_probability) - p.probability)
                   > tolerance * std::max(p.probability, std::exp(p.log_probability))) {
                throw std::invalid_argument("invalid probability or inconsistent log probability");
            }
            mass.add(p.probability);
            const bool target = p.candidate_id == sample.expected_candidate_id;
            const auto difference = p.probability - (target ? 1.L : 0.L);
            sample_brier.add(difference * difference);
            if (target) { target_found = true; target_log_p = p.log_probability; }
            if (p.candidate_id == prediction.decision.candidate_id) {
                selected_found = true;
                selected_p = p.probability;
            }
        }
        if (!target_found || !selected_found || std::abs(mass.value() - 1) > tolerance) {
            throw std::invalid_argument("target and decision must be in a normalized distribution");
        }
        const bool hit = prediction.decision.candidate_id == sample.expected_candidate_id;
        const auto loss = -target_log_p;
        report.cases.push_back({sample.id, hit, selected_p, loss, sample_brier.value()});
        correct.add(hit ? 1 : 0);
        if (std::isinf(loss)) infinite_loss = true;
        else log_loss.add(loss / count);
        brier.add(sample_brier.value());
        const auto bin = selected_p == 1 ? bins - 1
            : std::min(bins - 1, static_cast<std::size_t>(selected_p * static_cast<long double>(bins)));
        ++report.reliability[bin].count;
        bin_probabilities[bin].add(selected_p);
        bin_correct[bin].add(hit ? 1 : 0);
    }
    report.accuracy = correct.value() / count;
    report.mean_log_loss = infinite_loss ? std::numeric_limits<long double>::infinity()
        : detail::finite(log_loss.value());
    report.mean_brier_score = brier.value() / count;
    detail::CompensatedSum calibration;
    for (std::size_t i = 0; i < bins; ++i) {
        auto& bin = report.reliability[i];
        if (bin.count == 0) continue;
        const auto size = static_cast<long double>(bin.count);
        bin.mean_selected_probability = bin_probabilities[i].value() / size;
        bin.accuracy = bin_correct[i].value() / size;
        calibration.add(size / count * std::abs(bin.mean_selected_probability - bin.accuracy));
    }
    report.expected_calibration_error = calibration.value();
    return report;
}

} // namespace iiDecision
