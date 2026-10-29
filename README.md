# iiDecision 0.0.5

Sources are grouped under `src/` by decision, evaluation, RAG, Laya,
acceleration, and backend responsibility; paired interfaces and implementations
remain together. Existing `<iiDecision/...>` public include names are unchanged.

Native Metal and Vulkan acceleration is supplied by the separately built
[GPU application adapter](examples/gpu-adapter). The application links iiDecision
and iiGPU 0.1+ independently and injects the discovered `IEvidenceBackend`
instances. The exact integer evidence kernel, device IDs, pipeline and buffer
reuse, and precision guarantees are retained. Metal and Vulkan are no longer
automatically discovered by the iiDecision library; applications using those
backends must register the adapter. See [the native acceleration contract](docs/Acceleration.md).

[Windows build and execution](docs/WindowsBuildAndRun.md)

Package release: **0.0.5** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


A C++23 decision-making library that takes a Context and multiple Candidates and returns one Decision. Consumers request a decision through the `IDecisionMaker` contract. The core types do not depend on a particular game, NPC, or task system.

**The product's purpose is precise probability calculation, with priority over fast responses.** It exhaustively calculates finite joint states without assuming that correlations are independent or arbitrarily excluding states with small probabilities. Numerical stability and verifiability take priority over throughput and latency.

The minimal `IDecisionMaker`, `Candidate`, and `Decision` contracts are retained. Categorical observations have been added to `Context`, and an empty `Context{}` is also valid. `FirstCandidateDecisionMaker` returns the first candidate, while `ExactDecisionMaker` calculates conditional probabilities given the observed evidence. It uses a C++23 core and optional native acceleration SDKs. There is no Qt dependency.

**Multiple available accelerators are used together within the bounds of preserving precision.** Direct execution paths for Metal, MLX, OpenCL, and Vulkan are available when their native SDKs are configured. Categorical evidence comparisons run on the device, while log-mass summation, conditional probabilities, and entropy retain the existing `long double` calculations. The number of devices and providers is not limited to one per platform. The actual execution path and CPU fallback execution are recorded in the report.

<a id="10줄-사용-예제"></a>

## 10-line usage example

The code below is identical to the [executable example](examples/first_candidate.cpp)and prints `A`.

```cpp
#include <iiDecision/FirstCandidateDecisionMaker.h>
#include <array>
#include <iostream>
int main()
{
    const std::array candidates{iiDecision::Candidate{"A"}, iiDecision::Candidate{"B"}, iiDecision::Candidate{"C"}};
    iiDecision::FirstCandidateDecisionMaker implementation;
    iiDecision::IDecisionMaker& maker = implementation;
    std::cout << maker.decide(iiDecision::Context{}, candidates).candidate_id << '\n';
}
```

<a id="빌드와-테스트"></a>

## Build and tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Build artifacts are placed in `build/`. The tests include verification against independent 90-digit reference values using the standard `decimal` module in Python 3. Python is not a runtime dependency of the library. A static library is the default; a shared library can be built with `BUILD_SHARED_LIBS=ON`.

<a id="cmake-패키지-사용"></a>

## Using the CMake package

```cmake
find_package(iiDecision 0.0.5 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiDecision::iiDecision)
```

The default installation path is `~/.local/SDK/iiDecision` and can be changed with `CMAKE_INSTALL_PREFIX` or `cmake --install build --prefix <path>`. The batch installer at the SDK root automatically discovers this project.

<a id="확률-계산과-평가"></a>

## Probability calculation and evaluation

The [probability example](examples/probability.cpp)takes the joint states' log weights and observations as input. `ExactDecisionMaker::evaluate` returns all candidates' probabilities and log probabilities, the selection result, the log probability of the evidence, entropy, the normalization residual, and the number of linear-probability underflows. `decide` returns only the selection result from the same calculation through the existing contract.

```cpp
const iiDecision::Context context{{{"signal", "yes"}}};
const auto prediction = maker.evaluate(context, candidates);
// prediction.probabilities also contains every unselected candidate.
```

`evaluatePredictions` calculates accuracy, log loss, multiclass Brier score, reliability bins, ECE, and per-case scores for evaluation cases with ground-truth answers. Evaluation data separate from the data used for training or threshold adjustment must be used.

The term exact in exhaustive calculation refers to the inference algorithm. Numbers are represented in `long double` floating point, and the actual number of mantissa bits on each platform is reported by `probability_mantissa_bits`. This does not mean arbitrary-precision calculation or a guarantee of real-world accuracy. Log probabilities are preserved even when linear probabilities underflow. Distribution concentration and numerical residuals are distinguished from calibration on real data.

<a id="네이티브-하드웨어-가속"></a>

## Native hardware acceleration

```cpp
auto hardware = std::make_shared<iiDecision::EvidenceMatcher>();
iiDecision::ExactDecisionMaker maker(states, hardware);
const auto prediction = maker.evaluate(context, candidates);
for (const auto& run : prediction.execution.records) {
    // run.backend_id, run.row_count, run.completed, run.diagnostic
}
```

By default, state rows are distributed across the discovered accelerators and executed concurrently. Failed ranges are calculated on the CPU, and the causes of failure are recorded. To prohibit CPU fallback execution, use `AccelerationOptions::mode = ExecutionMode::RequireAccelerators`. Accelerator selection, provider options, plugin registration, platform-specific preparation, and verified limitations are described in
the [acceleration document](docs/Acceleration.md).

The library options `IIDECISION_MLX` and `IIDECISION_OPENCL` accept `AUTO`,
`ON`, or `OFF`. Existing `IIDECISION_METAL` and `IIDECISION_VULKAN` cache
settings no longer enable built-in backends; use the application adapter.
ONNX Runtime support has
been removed. No provider plugins or model runtime are discovered for it.

## Retired Laya model backend

The previous learned Laya backend, exporter, downloader, and tokenizer build are
retired. `LayaDecisionMaker::available()` returns false and constructing a model
reports an explicit error. Exact probability calculation and the native evidence
interfaces remain available. See [the retirement contract](docs/RuntimeRetirement.md).

## Evidence-aware RAG decisions

`RagDecisionGate` converts relevance and answerability judgments into `Answer`,
`RetrySearch`, or `Escalate`. Text and image evidence have separate runtime thresholds.
Accepted evidence is ranked, merged into unique document/page citations, and bounded
by a page budget. Reports retain every candidate, rejection reason, and policy revision.
`run` performs at most one expanded retrieval before escalation.

```cpp
#include <iiDecision/RagDecisionGate.h>
#include <array>

const std::array evidence{
    iiDecision::RagEvidence{"passage", "guide", 17,
        iiDecision::EvidenceModality::Text, 0.9L, 0.95L}
};
const auto report = iiDecision::RagDecisionGate{}.evaluate(evidence);
// report.action == iiDecision::RagAction::Answer; cite report.pages.
```

The previous learned evidence scorer is retired with the Laya model backend.
`RagDecisionGate` remains an independent contract for validated evidence supplied
by supported application components. It performs no implicit model inference.
See the [RAG contract and examples](docs/AgenticRag.md).

I/O and lifetime rules are defined in the [contract document](docs/Contract.md); formulas and precision policies in
the [probability-model document](docs/Probability.md); the review of materials and reasons for adoption in
the [research-review document](docs/Research.md); and TDD and package verification in
the [verification document](docs/Verification.md).

## License

SPDX-License-Identifier: MIT

The original code and documentation in this repository are licensed under the
[MIT License](LICENSE). Third-party code, libraries, fonts, tools, and model
weights retain their respective licenses.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
