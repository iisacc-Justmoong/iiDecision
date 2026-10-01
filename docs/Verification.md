# iiDecision 0.0.1 검증

## TDD

공개 계약과 테스트를 먼저 정의하였다. 빈 Decision을 반환하는 임시 구현에서
다음 두 테스트가 모두 실패하는 것을 확인한 뒤 첫 후보 선택 구현을 완성하였다.

- `FirstCandidateDecisionMaker.SelectsFirstCandidate`: `IDecisionMaker&`로
  A·B·C를 전달하여 A가 선택되는지 확인한다.
- `FirstCandidateDecisionMaker.RejectsEmptyCandidates`: 빈 목록이
  `std::invalid_argument`로 거부되는지 확인한다.

`iiDecision.Example`은 README의 10줄 예제를 컴파일하고 출력 A를 확인한다.
테스트는 Release에서도 실행되는 명시적 조건 검사를 사용한다.

## 빌드와 설치 패키지

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/contract-stage"
cmake -S tests/consumer -B build/consumer/build -DCMAKE_BUILD_TYPE=Release -DiiDecision_DIR="$PWD/build/contract-stage/lib/cmake/iiDecision"
cmake --build build/consumer/build --parallel 2
ctest --test-dir build/consumer/build --output-on-failure
```

별도 소비 프로젝트는 `find_package(iiDecision 0.0.1 EXACT CONFIG REQUIRED)`와
`iiDecision::iiDecision`만 사용하여 설치된 공개 헤더와 라이브러리를 검증한다.
동일한 검증을 `BUILD_SHARED_LIBS=ON`과 `build/contract-shared-stage`에서도 수행한다.

## 실행 결과

2026-10-01 macOS arm64, Release 빌드에서 다음 결과를 확인하였다.
모든 빌드 및 임시 설치 산출물은 프로젝트의 `build/` 아래에 생성하였다.

| 구성 | 전체 빌드 | 단위 테스트 및 예제 | 설치 패키지 소비 테스트 |
| --- | --- | --- | --- |
| 정적 라이브러리 | 성공 | 3/3 통과 | 1/1 통과 |
| 공유 라이브러리 | 성공 | 3/3 통과 | 1/1 통과 |

README의 C++ 예제가 정확히 10줄이고 컴파일한 예제 파일과 같은 내용임을
확인하였다. 실제 제품과의 통합은 이번 검증 범위에 포함하지 않는다.
