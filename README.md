# iiDecision 0.0.5

Package release: **0.0.5** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


A C++23 decision-making library that takes a Context and multiple Candidates and returns one Decision. Consumers request a decision through the `IDecisionMaker` contract. The core types do not depend on a particular game, NPC, or task system.

**The product's purpose is precise probability calculation, with priority over fast responses.** It exhaustively calculates finite joint states without assuming that correlations are independent or arbitrarily excluding states with small probabilities. Numerical stability and verifiability take priority over throughput and latency.

The minimal `IDecisionMaker`, `Candidate`, and `Decision` contracts are retained. Categorical observations have been added to `Context`, and an empty `Context{}` is also valid. `FirstCandidateDecisionMaker` returns the first candidate, while `ExactDecisionMaker` calculates conditional probabilities given the observed evidence. It uses a C++23 core and optional native acceleration SDKs. There is no Qt dependency.

**Multiple available accelerators are used together within the bounds of preserving precision.** Direct execution paths for Metal, MLX, OpenCL, and Vulkan, along with GPU and NPU providers from ONNX Runtime, have been connected. Categorical evidence comparisons run on the device, while log-mass summation, conditional probabilities, and entropy retain the existing `long double` calculations. The number of devices and providers is not limited to one per platform. The actual execution path and CPU fallback execution are recorded in the report.

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

The CMake options `IIDECISION_METAL`, `IIDECISION_MLX`, `IIDECISION_OPENCL`, `IIDECISION_VULKAN`, and `IIDECISION_ONNX` each accept `AUTO`, `ON`, or `OFF`. The default `AUTO` discovers installed SDKs and includes them in the build; `ON` makes configuration fail if the SDK is absent. The CPU core can also be built on platforms without these SDKs. Specify the C++ SDK paths with `ONNXRuntime_ROOT` for ONNX Runtime and `MLX_DIR` for MLX. Python bindings are not included as runtime dependencies.

## Laya model backend

`LayaDecisionMaker` adds local compatibility with the official [Laya decision model](https://huggingface.co/convaiinnovations/laya).
It selects actions from a supplied state and options through the existing `IDecisionMaker` contract, and also exposes
typed `Choice`, `Score`, and `Noul` questions in a single ONNX forward pass. This learned model backend is separate
from exhaustive finite-state probability inference.

```cpp
#include <iiDecision/LayaDecisionMaker.h>
#include <array>

iiDecision::LayaDecisionMaker maker("local-laya-package");
const std::array questions{
    iiDecision::LayaQuestion{"route", "Choose the next action.",
        iiDecision::LayaQuestionType::Choice,
        {{"refund", "Issue a refund"}, {"support", "Provide technical support"}}}
};
const auto answers = maker.predict("The customer reports an application crash.", questions);
```

`IIDECISION_LAYA=AUTO|ON|OFF` controls the backend. It needs the ONNX Runtime C++ SDK and Cargo at build time;
the native Hugging Face tokenizer archive is included in the installed CMake package. Inference uses local
ONNX Runtime CPU execution without Qt, Python, or a hosted service. The checkpoint must first be exported
using `tools/export_laya.py`; Python and the pinned official `laya==0.3.24` package are preparation tools only.
See [Laya compatibility and model preparation](docs/Laya.md) for the input contract, token budgets,
temperature scaling, upstream attribution, and the verified scope.

I/O and lifetime rules are defined in the [contract document](docs/Contract.md); formulas and precision policies in
the [probability-model document](docs/Probability.md); the review of materials and reasons for adoption in
the [research-review document](docs/Research.md); and TDD and package verification in
the [verification document](docs/Verification.md). This repository has not been granted a separate license permitting its use.
