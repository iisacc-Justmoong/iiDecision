# iiDecision

LLM이 개입하기 전에 상태를 평가하여 정해진 선택지와 확률 분포를 제공하는 C++23
의사결정 모델 라이브러리이다. Qt, 네트워크 서비스, 외부 ML 런타임 의존성이 없다.

0.1.0의 범위는 수치 특징 기반 선형 softmax 모델, 선택지별 확률, 최상위 선택,
상위 두 선택의 확률 격차, 기본 지도학습이다. 입력 특징과 선택지의 의미는 호출자가
정의한다. 자연어를 이해하는 사전학습 모델이나 Jev의 모델 가중치를 제공하지 않는다.

## 빌드와 검증

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

빌드 산출물은 `build/`에만 둔다. 기본 설치 경로는 `~/.local/SDK/iiDecision`이며
`CMAKE_INSTALL_PREFIX` 또는 `cmake --install build --prefix <경로>`로 변경할 수 있다.
`BUILD_SHARED_LIBS=ON`은 공유 라이브러리를 빌드한다. 기본값은 정적 라이브러리이다.
SDK 루트의 일괄 설치기는 이 프로젝트를 자동 발견하며 별도 등록이 필요 없다.

## 사용

```cmake
find_package(iiDecision 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiDecision::iiDecision)
```

```cpp
#include <iiDecision/DecisionModel.h>
#include <array>

auto model = iiDecision::DecisionModel::create({{
    {"local", "Local workflow", {-4.0, -4.0}, 4.0},
    {"llm", "Language model", {4.0, 4.0}, -4.0},
}});
if (!model) return;
const std::array features{0.1, 0.2};
auto decision = model->predict(features);
if (!decision) return;
const auto& selected = decision->choices[decision->selected_index];
// selected.id와 선택지 전체의 probability를 호출자의 작업 분기에 사용한다.
```

동일한 모델의 파라미터는 실제 정답 선택이 있는 데이터로 학습할 수 있다.

```cpp
const std::array examples{
    iiDecision::TrainingExample{{0.1, 0.2}, "local"},
    iiDecision::TrainingExample{{0.9, 0.8}, "llm"},
};
auto trained = model->train(examples, {.epochs = 200, .learning_rate = 0.1});
// 실패하면 model의 기존 파라미터를 유지한다.
```

`confidence`는 최상위 선택의 확률이며 경험적으로 보정된 정답률이 아니다. 판단
임계값과 LLM 호출·실제 작업 실행은 소비자가 결정한다. 모델은 텍스트를 생성하거나
작업을 실행하지 않는다. 학습과 오류 계약은 [모델 문서](docs/Model.md), 설치된
패키지 소비 검증은 [검증 문서](docs/Verification.md)에 정의한다.

소스와 공개 헤더는 `src/`, 테스트는 `tests/`, 예제는 `examples/`, 패키지 설정은
`cmake/`에 둔다. 이 저장소에는 별도 사용 허락 라이선스를 부여하지 않았다.
