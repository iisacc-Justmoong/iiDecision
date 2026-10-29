#include <iiDecision/Acceleration.h>
#include <iiDecision/ExactDecisionMaker.h>
#ifdef IIDECISION_TEST_GPU_ADAPTER
#include "GpuAdapter.h"
#endif

#include <atomic>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace iiDecision;

namespace {
std::vector<std::shared_ptr<IEvidenceBackend>> nativeGpuBackends() {
#ifdef IIDECISION_TEST_GPU_ADAPTER
    auto result = gpu_adapter::discover(gpu_adapter::GpuApi::metal);
    auto vulkan = gpu_adapter::discover(gpu_adapter::GpuApi::vulkan);
    result.insert(result.end(),vulkan.begin(),vulkan.end());
    return result;
#else
    return {};
#endif
}
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Exception, class F> void rejects(F f) {
    try { f(); } catch (const Exception&) { return; }
    throw std::runtime_error("expected exception");
}
class Fake final : public IEvidenceBackend {
public:
    std::string id;
    bool fail = false, invalid = false, detected = true;
    std::atomic<std::size_t> calls{0};
    explicit Fake(std::string name) : id(std::move(name)) {}
    BackendInfo info() const override { return {id, "test", id, DeviceKind::Gpu, detected, "test device availability"}; }
    std::vector<std::uint8_t> match(std::size_t rows, std::span<const std::uint32_t> values,
        std::span<const std::uint32_t> query) override {
        ++calls;
        if (fail) throw std::runtime_error("device failed");
        std::vector<std::uint8_t> result(rows, 1);
        for (std::size_t r = 0; r < rows; ++r)
            for (std::size_t c = 0; c < query.size(); ++c)
                if (values[r * query.size() + c] != query[c]) result[r] = 0;
        if (invalid && rows) result[0] = 2;
        return result;
    }
};
void scalar() {
    EvidenceMatcher matcher({.mode=ExecutionMode::CpuOnly});
    const std::vector<std::uint32_t> values{0xffffffff, 7, 0xfffffffe, 7, 0xffffffff, 8};
    const std::vector<std::uint32_t> query{0xffffffff, 7};
    const auto result = matcher.match(3, values, query);
    check(result.mask == std::vector<std::uint8_t>{1,0,0}, "uint32 IDs must compare exactly");
    check(result.execution.cpu_rows == 3, "CPU execution must be visible");
    check(matcher.match(3, {}, {}).mask == std::vector<std::uint8_t>{1,1,1}, "empty evidence");
    check(matcher.match(0, {}, query).mask.empty(), "empty batch");
    rejects<std::invalid_argument>([&]{matcher.match(4, values, query);});
    rejects<std::invalid_argument>([&]{matcher.match(std::numeric_limits<std::size_t>::max(), {}, query);});
}
void scheduling() {
    auto a = std::make_shared<Fake>("fake/a"), b = std::make_shared<Fake>("fake/b");
    EvidenceMatcher matcher({.mode=ExecutionMode::RequireAccelerators, .backend_ids={a->id,b->id}}, {a,b});
    const std::vector<std::uint32_t> values{5,6,5,6,5,6,5}, query{5};
    auto result = matcher.match(7,values,query);
    check(result.mask == std::vector<std::uint8_t>{1,0,1,0,1,0,1}, "merged row order");
    check(a->calls == 1 && b->calls == 1, "use both selected backends");
    check(result.execution.records.size() == 2 && result.execution.cpu_rows == 0, "device records");
    b->fail = true;
    rejects<std::runtime_error>([&]{matcher.match(7,values,query);});
    EvidenceMatcher fallback({.backend_ids={a->id,b->id}}, {a,b});
    result = fallback.match(7,values,query);
    check(result.mask == std::vector<std::uint8_t>{1,0,1,0,1,0,1}, "fallback result");
    check(result.execution.cpu_rows == 3, "only failed shard falls back");
    check(!result.execution.records[1].completed, "failure must be visible");
    b->fail = false; b->invalid = true;
    rejects<std::runtime_error>([&]{matcher.match(7,values,query);});
    b->invalid = false; b->detected = false;
    const auto calls = b->calls.load();
    rejects<std::runtime_error>([&]{matcher.match(7,values,query);});
    check(b->calls == calls, "unavailable backend must not execute");
    EvidenceMatcher strict_cpu({.mode=ExecutionMode::RequireAccelerators,.backend_ids={"cpu"}});
    rejects<std::runtime_error>([&]{strict_cpu.match(7,values,query);});
    rejects<std::invalid_argument>([&]{EvidenceMatcher bad({.backend_ids={"missing"}}, {a});});
    rejects<std::invalid_argument>([&]{EvidenceMatcher bad({}, {a,a});});
}
void integration() {
    auto backend = std::make_shared<Fake>("fake/model");
    auto matcher = std::make_shared<EvidenceMatcher>(
        AccelerationOptions{.mode=ExecutionMode::RequireAccelerators, .backend_ids={backend->id}},
        std::vector<std::shared_ptr<IEvidenceBackend>>{backend});
    std::vector<JointState> states{{"A",{{"x","0"}},0}, {"A",{{"x","1"}},-10000},
        {"B",{{"x","0"}},1}, {"B",{{"x","1"}},-10000+std::log(3.L)}};
    ExactDecisionMaker accelerated(states, matcher);
    auto cpu = std::make_shared<EvidenceMatcher>(AccelerationOptions{.mode=ExecutionMode::CpuOnly});
    ExactDecisionMaker reference(states,cpu);
    const Candidate candidates[]{{"A"},{"B"}};
    const Context query{{{"x","1"}}};
    const auto actual = accelerated.evaluate(query,candidates), expected = reference.evaluate(query,candidates);
    check(actual.decision.candidate_id == "B", "rare evidence decision");
    for (std::size_t i = 0; i < 2; ++i) {
        check(actual.probabilities[i].probability == expected.probabilities[i].probability, "probability arithmetic unchanged");
        check(actual.probabilities[i].log_probability == expected.probabilities[i].log_probability, "log arithmetic unchanged");
    }
    check(actual.execution.cpu_rows == 0 && backend->calls == 1, "model executes backend");
}
int devices(const std::string& only,bool include_cpu = false) {
    const auto injected = nativeGpuBackends();
    EvidenceMatcher discovery({},injected);
    bool executed = false;
    for (const auto& info : discovery.backends()) {
        std::cout << info.id << " | " << info.api << " | " << info.device << " | " << info.available << " | " << info.diagnostic << '\n';
        if (!info.available || (info.kind == DeviceKind::Cpu && !include_cpu) || (!only.empty() && !info.id.starts_with(only))) continue;
        const auto mode = include_cpu ? ExecutionMode::PreferAccelerators : ExecutionMode::RequireAccelerators;
        EvidenceMatcher matcher({.mode=mode, .backend_ids={info.id}},injected);
        std::vector<std::uint32_t> query{0xffffffff, 0x1000001, 2049};
        std::vector<std::uint32_t> values;
        std::vector<std::uint8_t> expected;
        for (std::uint32_t r = 0; r < 259; ++r) {
            auto row = query;
            if (r % 4) row[r % 3] ^= (1u << (r % 32));
            values.insert(values.end(),row.begin(),row.end());
            expected.push_back(r % 4 == 0);
        }
        try {
            const auto actual = matcher.match(expected.size(),values,query);
            check(actual.mask == expected, "native backend lost category bits");
            check(actual.execution.records.size() == 1 && actual.execution.records[0].completed &&
                actual.execution.records[0].backend_id == info.id,"requested backend did not execute");
            check(actual.execution.cpu_rows == (include_cpu ? expected.size() : 0), "native backend execution kind differs");
            // Device filtering must preserve the entire precision contract, including
            // correlated evidence, rare mass, underflow and caller-order ties.
            auto runtime = std::make_shared<EvidenceMatcher>(AccelerationOptions{
                .mode=mode,.backend_ids={info.id}},injected);
            auto cpu = std::make_shared<EvidenceMatcher>(AccelerationOptions{.mode=ExecutionMode::CpuOnly});
            const Candidate candidates[]{{"C"},{"A"},{"B"}};
            for (int fixture = 0; fixture < 4; ++fixture) {
                std::vector<JointState> states;
                for (int id = 0; id < 3; ++id) for (int x = 0; x < 4; ++x) for (int y = 0; y < 3; ++y) {
                    const auto weight = fixture == 0 ? static_cast<long double>((id+1)*(x+1)+y) :
                        fixture == 1 ? -10000.L*(x != 0)+id :
                        fixture == 2 ? -2.L*std::numeric_limits<long double>::max_exponent*std::log(2.L)*id : 0.L;
                    states.push_back({std::string(1,static_cast<char>('A'+id)),
                        {{"x",std::to_string(x)},{"y",std::to_string(y)}},weight});
                }
                ExactDecisionMaker model(states,runtime), reference(states,cpu);
                for (int x = -1; x < 4; ++x) for (int y = -1; y < 3; ++y) {
                    Context context;
                    if (x >= 0) context.observations.push_back({"x",std::to_string(x)});
                    if (y >= 0) context.observations.push_back({"y",std::to_string(y)});
                    const auto result = model.evaluate(context,candidates), expected_report = reference.evaluate(context,candidates);
                    check(result.decision.candidate_id == expected_report.decision.candidate_id, "native decision differs");
                    check(result.log_evidence_probability == expected_report.log_evidence_probability &&
                        result.entropy == expected_report.entropy && result.normalization_error == expected_report.normalization_error &&
                        result.underflowed_candidates == expected_report.underflowed_candidates, "native diagnostics differ");
                    for (std::size_t i = 0; i < 3; ++i)
                        check(result.probabilities[i].probability == expected_report.probabilities[i].probability &&
                            result.probabilities[i].log_probability == expected_report.probabilities[i].log_probability,
                            "native posterior differs from precise CPU calculation");
                    if (!context.observations.empty() && !include_cpu) check(result.execution.cpu_rows == 0,"model silently fell back");
                }
            }
            std::cout << "EXECUTED " << info.id << " rows=" << expected.size() << '\n';
            executed = true;
        } catch (const std::exception& error) {
            if (!only.empty()) throw;
            std::cout << "UNSUPPORTED " << info.id << " " << error.what() << '\n';
        }
    }
    return executed ? 0 : 77;
}
int reuse(const std::string& prefix) {
    const auto injected = nativeGpuBackends();
    EvidenceMatcher discovery({},injected);
    for (const auto& info : discovery.backends()) {
        if (!info.available || info.kind == DeviceKind::Cpu || !info.id.starts_with(prefix)) continue;
        EvidenceMatcher matcher({.mode=ExecutionMode::RequireAccelerators,.backend_ids={info.id}},injected);
        const std::vector<std::uint32_t> query{UINT32_MAX,16777217};
        for (std::size_t rows : {1u,65u,3u,1025u,17u,259u}) {
            std::vector<std::uint32_t> values;
            std::vector<std::uint8_t> expected;
            for (std::size_t i=0;i<rows;++i) {
                values.push_back(i%2?UINT32_MAX-1:UINT32_MAX); values.push_back(query[1]);
                expected.push_back(i%2?0:1);
            }
            const auto result=matcher.match(rows,values,query);
            check(result.mask==expected,"reused GPU buffers lost exact categorical results");
            check(result.execution.cpu_rows==0&&result.execution.records.size()==1&&result.execution.records[0].completed,
                "reused GPU pipeline silently fell back");
        }
        std::cout<<"REUSED "<<info.id<<" shapes=6 exact_uint32=true cpu_rows=0\n";
        return 0;
    }
    return 77;
}
#ifdef IIDECISION_TEST_GPU_ADAPTER
int sharedGpuConcurrency() {
    auto backends = nativeGpuBackends();
    std::shared_ptr<IEvidenceBackend> selected;
    for (const auto& backend : backends) {
        const auto info = backend->info();
        if (info.available && info.kind == DeviceKind::Gpu) { selected=backend; break; }
    }
    if (!selected) return 77;
    const auto id = selected->info().id;
    const AccelerationOptions options{.mode=ExecutionMode::RequireAccelerators,.backend_ids={id}};
    EvidenceMatcher first(options,{selected}), second(options,{selected});
    const std::vector<std::uint32_t> query{UINT32_MAX,16777217};
    const std::vector<std::uint32_t> values{UINT32_MAX,16777217,0,16777217};
    std::exception_ptr first_error, second_error;
    auto exercise = [&](EvidenceMatcher& matcher,std::exception_ptr& error) {
        try {
            for (int i=0;i<8;++i) {
                const auto result=matcher.match(2,values,query);
                check(result.mask==std::vector<std::uint8_t>{1,0} && result.execution.cpu_rows==0,
                      "shared GPU adapter returned an incorrect result");
            }
        } catch (...) { error=std::current_exception(); }
    };
    std::jthread a([&] { exercise(first,first_error); });
    std::jthread b([&] { exercise(second,second_error); });
    a.join(); b.join();
    if(first_error) std::rethrow_exception(first_error);
    if(second_error) std::rethrow_exception(second_error);
    return 0;
}
#endif
void allDevices() {
    EvidenceMatcher matcher({.mode=ExecutionMode::RequireAccelerators},nativeGpuBackends());
    std::size_t available = 0;
    for (const auto& device : matcher.backends()) if (device.available && device.kind != DeviceKind::Cpu) ++available;
    if (!available) throw std::runtime_error("no hardware for simultaneous execution");
    std::vector<std::uint32_t> values(1024), query{42};
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = i % 2 ? 42 : 43;
    const auto result = matcher.match(values.size(),values,query);
    check(result.execution.records.size() == available && result.execution.cpu_rows == 0,"all available backends must execute");
    for (std::size_t i = 0; i < values.size(); ++i) check(result.mask[i] == i % 2,"simultaneous result");
    for (const auto& record : result.execution.records)
        std::cout << "EXECUTED " << record.backend_id << " first=" << record.first_row << " rows=" << record.row_count << '\n';
}
}
int main(int argc, char** argv) {
    try {
        const std::string name = argc > 1 ? argv[1] : "Scalar";
        if (name == "Scalar") scalar();
        else if (name == "Scheduling") scheduling();
        else if (name == "Integration") integration();
        else if (name == "Devices") return devices(argc > 2 ? argv[2] : "");
        else if (name == "Reuse") return reuse(argc > 2 ? argv[2] : "metal/");
#ifdef IIDECISION_TEST_GPU_ADAPTER
        else if (name == "SharedGpuConcurrency") return sharedGpuConcurrency();
#endif
        else if (name == "AllDevices") allDevices();
        else throw std::invalid_argument("unknown test");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
