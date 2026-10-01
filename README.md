# iiDecision 0.0.1

Context와 여러 Candidate를 받아 하나의 Decision을 반환하는 C++23 의사결정
라이브러리이다. 소비자는 `IDecisionMaker` 계약을 통해 판단을 요청한다.
코어 타입은 특정 게임, NPC, 작업 시스템에 의존하지 않는다.

0.0.1은 `Candidate`, 빈 `Context`, `Decision`, `IDecisionMaker`와 구현체 하나인
`FirstCandidateDecisionMaker`를 제공한다. 이 구현은 입력 순서의 첫 후보를 선택하며,
빈 후보 목록에는 `std::invalid_argument`를 던진다. C++ 표준 라이브러리만 사용한다.
확률 모델과 학습 기능은 이 버전의 범위에 포함하지 않는다.

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

빌드 산출물은 `build/`에 둔다. CTest는 `IDecisionMaker&`를 통한 첫 후보 선택,
빈 목록 거부, README 예제 실행을 검증한다. 기본값은 정적 라이브러리이며
`BUILD_SHARED_LIBS=ON`으로 공유 라이브러리를 빌드할 수 있다.

## CMake 패키지 사용

```cmake
find_package(iiDecision 0.0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiDecision::iiDecision)
```

기본 설치 경로는 `~/.local/SDK/iiDecision`이며 `CMAKE_INSTALL_PREFIX` 또는
`cmake --install build --prefix <경로>`로 바꿀 수 있다. SDK 루트의 일괄 설치기는
이 프로젝트를 자동 발견한다.

입출력과 수명 규칙은 [계약 문서](docs/Contract.md), TDD 및 설치 패키지 검증은
[검증 문서](docs/Verification.md)에 정의한다. 이 저장소에는 별도 사용 허락
라이선스를 부여하지 않았다.
