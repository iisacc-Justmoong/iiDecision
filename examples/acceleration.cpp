#include <iiDecision/ExactDecisionMaker.h>
#include <iostream>
#include <cmath>

int main() {
    using namespace iiDecision;
    auto hardware = std::make_shared<EvidenceMatcher>();
    for (const auto& backend : hardware->backends())
        std::cout << backend.id << " available=" << backend.available << " device=" << backend.device
                  << " diagnostic=" << backend.diagnostic << '\n';
    ExactDecisionMaker model({{"A",{{"signal","yes"}},std::log(18.L)},
        {"A",{{"signal","no"}},std::log(2.L)}, {"B",{{"signal","yes"}},std::log(8.L)},
        {"B",{{"signal","no"}},std::log(72.L)}},hardware);
    const Candidate candidates[]{{"A"},{"B"}};
    const auto report = model.evaluate(Context{{{"signal","yes"}}},candidates);
    std::cout << "selected=" << report.decision.candidate_id << " cpu_rows=" << report.execution.cpu_rows << '\n';
    for (const auto& record : report.execution.records)
        std::cout << record.backend_id << " first=" << record.first_row << " rows=" << record.row_count
                  << " completed=" << record.completed << " diagnostic=" << record.diagnostic << '\n';
}
