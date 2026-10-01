# 검증

CTest는 확률 정규화·temperature·큰 logit·동률·단일 선택, 잘못된 파라미터와
상태, 데이터로부터 선택을 학습하는 동작, 중간 업데이트 이후 실패한 학습의 원자성, 인스턴스 독립성과
반복 예측의 결정성, LLM 앞단의 분기 예제를 확인한다. Release에서도 검증 조건을
실행하며 `assert`가 제거되는 컴파일 옵션에 의존하지 않는다.

설치된 CMake 패키지는 SDK 소스 경로를 참조하지 않는 독립 소비자로 검증한다.
아래 실행은 산출물을 모두 현재 저장소의 `build/` 아래에 둔다.

```sh
cmake --install build --prefix "$PWD/build/stage"
cmake -S tests/consumer -B build/consumer/build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/build/stage"
cmake --build build/consumer/build --parallel 2
ctest --test-dir build/consumer/build --output-on-failure
```

기본 빌드는 C++23 표준 라이브러리만 요구한다. macOS에서 통과한 결과는 다른
플랫폼에서 실행한 증거를 대신하지 않는다. 통계 모델의 실제 품질과 확률 보정은
제품 데이터로 별도로 평가해야 한다.

## 0.1.0 검증 기록

2026-10-01, macOS arm64 / Apple Clang 21.0.0 / Release에서 확인한 결과이다.

- 미구현 상태에서 6개 테스트가 실패함을 확인한 후 구현하였다.
- 정적 라이브러리: 기본 CTest 6/6, 설치된 패키지 독립 소비자 1/1 통과.
- 공유 라이브러리: 기본 CTest 6/6, 설치된 패키지 독립 소비자 1/1 통과.
- SDK 일괄 설치기의 자동 발견 결과: `iiDecision`, 로컬 SDK 의존성 없음.
- 패키지 검증 설치는 `build/stage`와 `build/shared-stage`에서 수행하였다.

다른 플랫폼 실행, 실제 업무 데이터 평가, 범용 자연어 판단 모델 학습은 이 기록의
범위가 아니다.
