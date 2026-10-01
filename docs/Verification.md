# iiDecision 0.0.2 검증

## TDD

공개 계약과 테스트를 먼저 정의하였다. 빈 Decision을 반환하는 임시 구현에서
다음 두 테스트가 모두 실패하는 것을 확인한 뒤 첫 후보 선택 구현을 완성하였다.

- `FirstCandidateDecisionMaker.SelectsFirstCandidate`: `IDecisionMaker&`로
  A·B·C를 전달하여 A가 선택되는지 확인한다.
- `FirstCandidateDecisionMaker.RejectsEmptyCandidates`: 빈 목록이
  `std::invalid_argument`로 거부되는지 확인한다.

`iiDecision.Example`은 README의 10줄 예제를 컴파일하고 출력 A를 확인한다.
테스트는 Release에서도 실행되는 명시적 조건 검사를 사용한다.

확률 계층은 사전 확률, 관측 후 확률, 상관관계, 평가 지표의 네 테스트가
임시 구현에서 실패하는 것을 먼저 확인한 뒤 구현하였다.
`build/probability-tdd-red.log`에 이 단계의 실패 기록을 보존하였다.

추가 검증은 드문 증거, 큰 양·음 로그 가중치, 후보·상태 순서, 스키마 오류,
중복 ID, 확률 0 증거, 동률, underflow와 구조적 0의 구분, 잘못된 평가 입력,
평균 log loss의 불필요한 overflow 방지이다. `iiDecision.ProbabilityExample`은
관측 후 A를 선택하고 평가 지표를 계산하는 예제를 실행한다.

## 빌드와 설치 패키지

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/probability-stage"
cmake -S tests/consumer -B build/consumer/build -DCMAKE_BUILD_TYPE=Release -DiiDecision_DIR="$PWD/build/probability-stage/lib/cmake/iiDecision"
cmake --build build/consumer/build --parallel 2
ctest --test-dir build/consumer/build --output-on-failure
```

별도 소비 프로젝트는 `find_package(iiDecision 0.0.2 EXACT CONFIG REQUIRED)`와
`iiDecision::iiDecision`만 사용하여 기존 계약, 확률 계산과 평가 API를 검증한다.
동일한 검증을 `BUILD_SHARED_LIBS=ON`과 `build/probability-shared-stage`에서도 수행한다.

## 독립 기준값과 감사 기록

`tests/decimal_oracle.py`는 Python 표준 Decimal의 90자리 정밀도로 결합 상태를
직접 조건화하여 C++ 결과와 비교한다. 19개 고정 모델 × 9개 관측 조합 = 171개
사례이다. ±10000 로그 가중치와 전역 질량이 매우 작은 관측 사례도 포함한다.
선택 후보, 확률, 로그 확률, 로그 증거 확률, 엔트로피와 underflow 진단을 확인한다.

허용 오차는 각 값에 대해 `64 * 2^(1-mantissa_bits) * max(1, abs(reference))`이다.
숫자 표현 능력이 다른 플랫폼에서는 해당 플랫폼의 실제 가수 비트를 사용한다.
이는 검증 fixture의 통과 기준이며 임의 입력 전체에 대한 엄밀한 오차 상한은 아니다.

CTest는 `build/decimal-oracle.json`에 입력 가중치·관측, 기준값·실제 결과,
사례별 오차, 빌드 설정, 실행 파일과 소스의 SHA-256을 기록한다. 유한값 밖의
숫자를 JSON number로 쓰지 않으며 정밀 수치는 문자열로 보존한다.
이번 검증의 구성별 기록은 `build/decimal-oracle-static.json`과
`build/decimal-oracle-shared.json`이다.

## 실행 결과

2026-10-01 macOS arm64, Release 빌드에서 다음 결과를 확인하였다.
모든 빌드 및 임시 설치 산출물은 프로젝트의 `build/` 아래에 생성하였다.

| 구성 | 전체 빌드 | 단위 테스트 및 예제 | 설치 패키지 소비 테스트 |
| --- | --- | --- | --- |
| 정적 라이브러리 | 성공 | 16/16 통과, Decimal 171/171 | 1/1 통과 |
| 공유 라이브러리 | 성공 | 16/16 통과, Decimal 171/171 | 1/1 통과 |

실제 `long double` 가수는 53비트이다. 정적·공유 구성에서 같은 오차를 확인하였다.

| 비교 값 | fixture에서 관측한 최대 절대 오차 |
| --- | --- |
| 후보 확률 | 2.2e-16 |
| 후보 로그 확률 | 약 1.61e-15 |
| 로그 증거 확률 | 약 5.47e-14 |
| 엔트로피 | 약 2.71e-16 |

큰 로그 값을 포함한 `error / max(1, abs(reference))`의 최댓값은 약 4.96e-16이다.

README의 C++ 예제가 정확히 10줄이고 컴파일한 예제 파일과 같은 내용임을
확인하였다. 실제 제품과의 통합은 이번 검증 범위에 포함하지 않는다.
