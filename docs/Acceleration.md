# 네이티브 가속과 정밀도 계약

## 목적과 계산 경계

iiDecision의 제품 목적은 낮은 지연시간보다 정밀한 의사결정 확률 계산이다.
가속을 켜도 전수 상태의 상관관계, 희소 질량, 로그 확률, 후보 순서에 따른
동률 규칙을 보존한다. GPU·NPU 사용을 이유로 확률 가중치를 FP16·FP32로
변환하거나 작은 확률을 버리지 않는다.

현재 실제 계산 경계는 다음과 같다.

1. 모델 구성 시 문자열 관측값을 키별 `uint32_t` 범주 ID로 인코딩한다.
2. 관측한 열만 모아 각 상태가 증거와 일치하는지 장치에서 계산한다.
3. 장치 작업 완료를 기다리고 상태마다 정확한 0·1 마스크를 받는다.
4. 원래 상태 순서로 마스크를 병합한다.
5. 로그 질량 합산·조건부 확률·엔트로피·정규화 잔차는 호스트의 기존
   `long double`·보상 합산·log-sum-exp 구현으로 계산한다.

따라서 확률 합산 전체를 GPU에 옮긴 구현은 아니다. NPU의 낮은 부동소수점
정밀도와 기존 확률 계약을 함께 만족시키기 위해 증거 필터링을 가속한다.
현재 작은 모델에서 속도가 개선된다는 보장은 없으며, 장치 초기화·복사·그래프
컴파일 비용이 더 클 수 있다. 처리 시간보다 계산 결과 보존을 우선한다.

직접 Metal·MLX·OpenCL·Vulkan 경로는 정수 비교를 실행한다. ONNX 경로와 제공자 설정은 폐기하였다.

## 제공되는 실제 실행 경로

### iiGPU 공통 명령 계층

Metal·Vulkan의 네이티브 장치·큐·버퍼·파이프라인·장벽·완료 대기는 독립 모듈
iiGPU 0.1 이상이 소유한다. 별도 소비자 프로젝트 `examples/gpu-adapter/`는 정확한
uint32 증거 커널·형상·마스크 해석을 제공하고 앱이 `IEvidenceBackend`로 주입한다.
Metal·Vulkan backend ID는
기존의 `metal/<index>`, `vulkan/<index>`를 유지한다.

동일 backend 슬롯은 기존 EvidenceMatcher 잠금으로 직렬화한다. 컴퓨트
파이프라인을 한 번 만들고 행렬·쿼리·출력 버퍼는 용량이 충분하면 재사용한다.
어댑터 자체도 접근을 직렬화하므로 동일 백엔드 인스턴스를 여러 matcher가
공유할 때 재사용 버퍼가 서로 덮어쓰이지 않는다.
행 수가 커지거나 작아져도 현재 유효한 범위를 바인딩하고 그 범위만 읽는다.
GPU 완료를 확인하기 전에 호스트가 버퍼를 덮어쓰지 않는다.

iiDecision 라이브러리는 iiGPU를 찾거나 링크하지 않는다. 애플리케이션이 두
패키지를 각각 찾고 어댑터의 `discover` 결과를 생성자의 `additional_backends`로
공급한다. 기존 `IIDECISION_METAL`·`IIDECISION_VULKAN` 캐시 값은 더 이상 내장
백엔드를 활성화하지 않으며 `ON`은 구성 시 경고한다. 이전 자동 탐색에 의존하던
앱은 이 등록 절차를 추가하고 다시 빌드해야 한다. iiDecision 공개 API에는
iiGPU나 네이티브 플랫폼 자료형을 추가하지 않는다.

```sh
cmake -S examples/gpu-adapter -B build/gpu-adapter \
  -DiiDecision_DIR="$HOME/.local/SDK/iiDecision/lib/cmake/iiDecision" \
  -DiiGPU_DIR="$HOME/.local/SDK/iiGPU/lib/cmake/iiGPU"
cmake --build build/gpu-adapter
ctest --test-dir build/gpu-adapter --output-on-failure
```

이 별도 소비자 프로젝트와 장치 테스트 소스는 설치 패키지의
`share/iiDecision/examples/gpu-adapter/`에도 포함된다. 실제 실행은 장치가
필요하며 단독 라이브러리의 빌드와 구분한다.

별도 어댑터 소비자의 네이티브 실행 검사는 `Acceleration.metal`, `Acceleration.vulkan`을 사용하고,
추가한 `Acceleration.metal_reuse`, `Acceleration.vulkan_reuse`는 같은 matcher에서
1·65·3·1025·17·259행을 순서대로 처리해 버퍼 재사용과 정확한 마스크를 검증한다.
CPU 대체 실행은 0행이어야 하며 기존 정밀도·희소 확률·동률 검증도 유지한다.

| 경로 | 실행 구현 | 장치와 플랫폼 범위 |
| --- | --- | --- |
| Metal | MSL 정수 compute kernel, 버퍼 업로드·dispatch·완료 확인·읽기 | macOS의 발견한 모든 Metal 장치, iOS의 기본 Metal 장치 |
| MLX C++ | GPU stream에서 uint32 `equal`과 행별 `all`, 명시적 `eval` | 설치된 MLX가 제공하는 Metal 또는 CUDA GPU 전체 |
| OpenCL 1.2+ | 정수 kernel 빌드, 모든 플랫폼·장치 열거, queue 실행·동기 읽기 | macOS, Windows, Linux와 지원되는 임베디드 드라이버 |
| Vulkan 1.1+ | SPIR-V 정수 compute pipeline, descriptor·buffer·fence·호스트 읽기 | Windows, Linux, Android, MoltenVK를 설치한 Apple 플랫폼 |
| C++23 CPU | 정확한 uint32 비교 | 위 SDK가 없는 플랫폼을 포함한 C++23 대상 |

MLX의 공식 설치 문서는 Metal 및 CUDA 빌드 경로를 제공한다.
[MLX 공식 문서](https://ml-explore.github.io/mlx/build/html/install.html).
OpenCL과 Vulkan의 실행·동기화 계약은 Khronos 규격에 따른다.
[OpenCL API](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html),
[Vulkan 명세](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html).

## 선택, 동시 실행과 실패 처리

```cpp
#include "GpuAdapter.h" // 애플리케이션의 어댑터 소스
auto metal = iiDecision::gpu_adapter::discover(iiDecision::gpu_adapter::GpuApi::metal);
auto vulkan = iiDecision::gpu_adapter::discover(iiDecision::gpu_adapter::GpuApi::vulkan);
metal.insert(metal.end(), vulkan.begin(), vulkan.end());
iiDecision::AccelerationOptions options;
options.mode = iiDecision::ExecutionMode::RequireAccelerators;
options.backend_ids = {"metal/0"};
auto hardware = std::make_shared<iiDecision::EvidenceMatcher>(options, metal);
iiDecision::ExactDecisionMaker model(states, hardware);
const auto result = model.evaluate(context, candidates);
```

ID는 `backends()`로 확인한 값을 사용한다. `available`은 장치 또는 제공자를
발견하였음을 나타내며, 해당 그래프를 실행할 수 있다는 보장은 아니다. SDK가
빠졌거나 초기화에 실패한 경로도 진단과 함께 반환한다.

- `CpuOnly`: 네이티브 가속 초기화 없이 CPU로 계산한다.
- `PreferAccelerators`: 기본값이다. 발견한 CPU 외의 모든 실행 경로를 선택한다.
  `backend_ids`를 지정하면 그 목록만 선택한다. 실패한 구간만 CPU로 계산한다.
- `RequireAccelerators`: 선택 경로의 실패나 가속기 부재가 오류가 된다.
  명시적으로 CPU를 선택하여 이 조건을 우회할 수 없다.

선택 경로들에 연속 상태 구간을 균등 분배하고 `std::async`로 실행한다. 행보다
경로가 많으면 앞의 경로부터 행 수만큼만 사용한다. 같은 물리 GPU에 Metal·MLX·
OpenCL·Vulkan 경로를 함께 실행하면 동일 하드웨어를 공유하므로 병렬 속도 향상을
보장하지 않는다. 원하는 경로만 지정하여 물리 장치 중복과 초기화 비용을 조정한다.

`ExecutionRecord`에는 경로 ID·시작 행·행 수·완료 여부·실패 원인을 기록한다.
기본 모드의 CPU 대체 실행은 별도 `cpu` 기록과 `cpu_rows`로 확인한다. 빈 증거는
모든 상태가 일치하는 항등 연산이므로 장치를 실행하지 않으며 엄격 모드에서도
유효하다. 빈 배치는 실행 기록 없이 빈 마스크를 반환한다.

각 `EvidenceMatcher` 인스턴스는 공유 구현으로 복사할 수 있다. 같은 인스턴스의
백엔드 호출은 잠금으로 직렬화하고 서로 다른 백엔드의 구간은 동시에 실행한다.
MLX의 lazy graph·stream 작업은 MLX 전역 잠금으로 보호한다. 추가로 전달한
사용자 백엔드를 서로 다른 matcher에 공유하면 그 백엔드의 동시 호출 안전성은
구현자가 보장해야 한다. 외부 백엔드는 입력 span을 반환 이후에 보관해서는 안 된다.

## 빌드와 의존성

`IIDECISION_MLX`, `IIDECISION_OPENCL`은
`AUTO`, `ON`, `OFF`를 지원한다. `ON`은 필요한 네이티브 SDK가 없으면 구성 오류를
반환한다. ONNX 제공자·플러그인 설정과 다운로드 도구는 폐기하였다.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMLX_DIR=/path/to/mlx/share/cmake/MLX
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Metal·Vulkan 경로는 앱 어댑터가 iiGPU의 장치·버퍼·명령 인터페이스를 사용한다. 여러 SDK를
함께 소비할 때 같은 MLX 패키지를 선택하고, 명시적으로 선택한 라이브러리와
실제로 로딩한 파일의 일치를 확인한다. CPU 확률 계산과 장치 실행 증거는 구분한다.

## 검증 범위

다음 장치 실행 기록은 어댑터 분리 전의 이력이다. 새 소비자 프로젝트의
실제 장치 실행은 별도의 검증이며 단독 빌드나 소비자 컴파일만으로 증명하지 않는다.

macOS arm64의 Apple M1 Max에서 여섯 경로의 실제 실행과 함께 동시 구간 실행을
확인하였다. 각 경로는 큰 uint32 ID와 259개 행의 꼬리 구간을 검증하며,
80개 추론 사례에서 정밀 CPU 결과와 전체 보고서 수치가 동일하였다.
이는 CoreML 제공자 실행 증거이며 ANE 실행 증거는 아니다.

가속 없는 구성, 정적·공유 라이브러리, 설치 소비 프로젝트 및 iOS arm64 Metal
크로스 빌드를 검증한다. 테스트가 사용할 하드웨어를 찾지 못하면 CTest에
`Skipped`를 반환하며 성공한 GPU 테스트로 세지 않는다.
Windows·Linux·Android 및 각 벤더 GPU·NPU의 물리 장치 실행은 현재 머신에서
확인하지 못하였다. 수동 실행 가능한 `native-build.yml`은 데스크톱 빌드·테스트와
Android·iOS 크로스 빌드를 정의하며, 이번 작업에서 호스팅 실행을 수행하지 않았다.
최종 구성별 결과는 [검증 문서](Verification.md)에 기록한다.
