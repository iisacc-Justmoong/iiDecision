# iiDecision 0.0.3

Context와 여러 Candidate를 받아 하나의 Decision을 반환하는 C++23 의사결정
라이브러리이다. 소비자는 `IDecisionMaker` 계약을 통해 판단을 요청한다.
코어 타입은 특정 게임, NPC, 작업 시스템에 의존하지 않는다.

**제품 목적은 빠른 응답보다 정밀한 확률 계산이다.** 유한 결합 상태를 전수 계산하며
상관관계를 독립으로 가정하거나 작은 확률의 상태를 임의로 제외하지 않는다.
수치 안정성과 검증 가능성을 처리량과 지연시간보다 우선한다.

최소 `IDecisionMaker`·`Candidate`·`Decision` 계약은 유지한다. `Context`에는
범주형 관측값을 추가하였으며 빈 `Context{}`도 유효하다. `FirstCandidateDecisionMaker`는
첫 후보를 반환하고, `ExactDecisionMaker`는 관측 증거의 조건부 확률을 계산한다.
C++23 코어와 선택적인 네이티브 가속 SDK를 사용한다. Qt 의존성은 없다.

**정밀도를 유지하는 범위에서 사용 가능한 여러 가속기를 함께 사용한다.**
Metal·MLX·OpenCL·Vulkan의 직접 실행 경로와 ONNX Runtime의 GPU·NPU 제공자를
연결하였다. 범주형 증거 비교를 장치에서 수행하며, 로그 질량 합산·조건부 확률·
엔트로피는 기존 `long double` 계산을 유지한다. 장치 수와 제공자 수를 플랫폼당
하나로 제한하지 않는다. 실제 실행 경로와 CPU 대체 실행은 보고서에 기록한다.

## 10줄 사용 예제

아래 코드는 [실행 가능한 예제](examples/first_candidate.cpp)와 같으며 `A`를 출력한다.

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

## 빌드와 테스트

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

빌드 산출물은 `build/`에 둔다. 테스트에는 Python 3의 표준 `decimal`을 사용하는
90자리 독립 기준값 검증이 포함된다. Python은 라이브러리 실행 의존성이 아니다.
기본값은 정적 라이브러리이며
`BUILD_SHARED_LIBS=ON`으로 공유 라이브러리를 빌드할 수 있다.

## CMake 패키지 사용

```cmake
find_package(iiDecision 0.0.3 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiDecision::iiDecision)
```

기본 설치 경로는 `~/.local/SDK/iiDecision`이며 `CMAKE_INSTALL_PREFIX` 또는
`cmake --install build --prefix <경로>`로 바꿀 수 있다. SDK 루트의 일괄 설치기는
이 프로젝트를 자동 발견한다.

## 확률 계산과 평가

[확률 예제](examples/probability.cpp)는 결합 상태의 로그 가중치와 관측값을 입력한다.
`ExactDecisionMaker::evaluate`는 모든 후보의 확률·로그 확률, 선택 결과, 증거의
로그 확률, 엔트로피, 정규화 잔차, 선형 확률의 underflow 개수를 반환한다.
`decide`는 동일한 계산의 선택 결과만 기존 계약으로 반환한다.

```cpp
const iiDecision::Context context{{{"signal", "yes"}}};
const auto prediction = maker.evaluate(context, candidates);
// prediction.probabilities에는 선택되지 않은 후보도 모두 존재한다.
```

`evaluatePredictions`는 정답이 있는 평가 사례에서 정확도, log loss, multiclass
Brier score, reliability bins, ECE와 사례별 점수를 계산한다. 학습이나 임계값
조정에 사용한 데이터와 별도의 평가 데이터를 사용해야 한다.

전수 계산의 exact는 추론 알고리즘의 의미이다. 숫자는 `long double` 부동소수점이며
플랫폼별 실제 가수 비트 수는 `probability_mantissa_bits`로 확인한다. 임의 정밀도
계산이나 실제 정답률 보장을 뜻하지 않는다. 로그 확률은 선형 확률이 underflow하는
경우에도 보존한다. 분포의 집중도와 수치 잔차는 실제 데이터에서의 calibration과
구분한다.

## 네이티브 하드웨어 가속

```cpp
auto hardware = std::make_shared<iiDecision::EvidenceMatcher>();
iiDecision::ExactDecisionMaker maker(states, hardware);
const auto prediction = maker.evaluate(context, candidates);
for (const auto& run : prediction.execution.records) {
    // run.backend_id, run.row_count, run.completed, run.diagnostic
}
```

기본값은 발견한 가속기들에 상태 행을 분배하여 동시에 실행한다. 실패한 구간은
CPU로 계산하고 실패 원인을 기록한다. CPU 대체 실행을 금지하려면
`AccelerationOptions::mode = ExecutionMode::RequireAccelerators`를 사용한다.
가속기 선택·제공자 옵션·플러그인 등록·플랫폼별 준비 방법과 확인된 한계는
[가속 문서](docs/Acceleration.md)에 설명하였다.

CMake 옵션 `IIDECISION_METAL`, `IIDECISION_MLX`, `IIDECISION_OPENCL`,
`IIDECISION_VULKAN`, `IIDECISION_ONNX`는 각각 `AUTO`·`ON`·`OFF`이다.
기본 `AUTO`는 설치된 SDK를 발견하여 빌드에 포함하며, `ON`은 SDK가 없으면
구성을 실패시킨다. SDK가 없는 플랫폼에서도 CPU 코어를 빌드할 수 있다.
ONNX Runtime은 `ONNXRuntime_ROOT`, MLX는 `MLX_DIR`로 C++ SDK 경로를 지정한다.
Python 바인딩은 런타임 의존성에 포함하지 않는다.

입출력과 수명 규칙은 [계약 문서](docs/Contract.md), 수식과 정밀도 정책은
[확률 모델 문서](docs/Probability.md), 문서 검토와 채택 이유는
[자료 검토 문서](docs/Research.md), TDD 및 패키지 검증은
[검증 문서](docs/Verification.md)에 정의한다. 이 저장소에는 별도 사용 허락
라이선스를 부여하지 않았다.
