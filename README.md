# iiDecision 0.0.2

Context와 여러 Candidate를 받아 하나의 Decision을 반환하는 C++23 의사결정
라이브러리이다. 소비자는 `IDecisionMaker` 계약을 통해 판단을 요청한다.
코어 타입은 특정 게임, NPC, 작업 시스템에 의존하지 않는다.

**제품 목적은 빠른 응답보다 정밀한 확률 계산이다.** 유한 결합 상태를 전수 계산하며
상관관계를 독립으로 가정하거나 작은 확률의 상태를 임의로 제외하지 않는다.
수치 안정성과 검증 가능성을 처리량과 지연시간보다 우선한다.

최소 `IDecisionMaker`·`Candidate`·`Decision` 계약은 유지한다. `Context`에는
범주형 관측값을 추가하였으며 빈 `Context{}`도 유효하다. `FirstCandidateDecisionMaker`는
첫 후보를 반환하고, `ExactDecisionMaker`는 관측 증거의 조건부 확률을 계산한다.
C++23 표준 라이브러리만 사용한다.

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
find_package(iiDecision 0.0.2 CONFIG REQUIRED)
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

입출력과 수명 규칙은 [계약 문서](docs/Contract.md), 수식과 정밀도 정책은
[확률 모델 문서](docs/Probability.md), 문서 검토와 채택 이유는
[자료 검토 문서](docs/Research.md), TDD 및 패키지 검증은
[검증 문서](docs/Verification.md)에 정의한다. 이 저장소에는 별도 사용 허락
라이선스를 부여하지 않았다.
