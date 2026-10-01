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

직접 Metal·MLX·OpenCL·Vulkan 경로는 정수 비교를 실행한다. ONNX Runtime
경로는 각 범주 ID를 독립적인 0·1 비트로 분해한 정적 형상의 그래프를 실행한다.
`Sub → Mul(delta, delta) → ReduceMax → Sub(1, mismatch)`의 모든 중간값은
-1·0·1이며, FP16에서도 정확히 표현 가능하다. ID 자체를 부동소수점으로
캐스팅하지 않으므로 2049, 16777217, UINT32_MAX도 서로 구분된다. 불필요한
상위 0비트는 생략하며 최대 32비트를 사용한다. 확률 가중치는 이 그래프에
들어가지 않는다.

## 제공되는 실제 실행 경로

| 경로 | 실행 구현 | 장치와 플랫폼 범위 |
| --- | --- | --- |
| Metal | MSL 정수 compute kernel, 버퍼 업로드·dispatch·완료 확인·읽기 | macOS의 발견한 모든 Metal 장치, iOS의 기본 Metal 장치 |
| MLX C++ | GPU stream에서 uint32 `equal`과 행별 `all`, 명시적 `eval` | 설치된 MLX가 제공하는 Metal 또는 CUDA GPU 전체 |
| OpenCL 1.2+ | 정수 kernel 빌드, 모든 플랫폼·장치 열거, queue 실행·동기 읽기 | macOS, Windows, Linux와 지원되는 임베디드 드라이버 |
| Vulkan 1.1+ | SPIR-V 정수 compute pipeline, descriptor·buffer·fence·호스트 읽기 | Windows, Linux, Android, MoltenVK를 설치한 Apple 플랫폼 |
| ONNX Runtime 1.24+ | 메모리 내 ONNX 그래프 생성, 제공자 등록, 장치별 세션·실행·결과 검증 | 해당 Runtime에 포함되거나 플러그인으로 등록한 CPU·GPU·NPU 제공자 |
| C++23 CPU | 정확한 uint32 비교 | 위 SDK가 없는 플랫폼을 포함한 C++23 대상 |

MLX의 공식 설치 문서는 Metal 및 CUDA 빌드 경로를 제공한다.
[MLX 공식 문서](https://ml-explore.github.io/mlx/build/html/install.html).
OpenCL과 Vulkan의 실행·동기화 계약은 Khronos 규격에 따른다.
[OpenCL API](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html),
[Vulkan 명세](https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html).

## ONNX Runtime의 여러 제공자와 장치

`GetAvailableProviders()`와 `Env::GetEpDevices()`를 사용한다. 새 EP API로
노출된 장치는 CPU·GPU·NPU 구분과 함께 모두 등록한다. 같은 제공자의 여러
장치, 여러 옵션 구성, 서로 다른 제공자를 동시에 선택할 수 있다. 네이티브
계산이 목적이므로 Azure와 JavaScript 제공자는 자동 구성에서 제외한다.

CUDA·TensorRT·MIGraphX·ROCm·CANN·oneDNN의 전용 C++ 등록 API와
CoreML·OpenVINO·QNN·XNNPACK·VitisAI·WebGPU의 Runtime 등록 API를
연결하였다. DirectML·NNAPI·ACL·ArmNN의 기존 factory header가 SDK에
있으면 해당 등록 API도 빌드한다. 새 EP 플러그인은 제공자 이름을 하드코딩하지
않고 장치 API로 연결한다. 기존 TVM·RKNPU 등 전용 factory를 추가하지 않은
빌드는 자동 발견만으로 그 제공자의 실행을 보장하지 않는다. 플러그인 EP 또는
`IEvidenceBackend` 구현을 사용해야 한다.

기존 API만 사용하는 Runtime의 GPU 제공자는 기본 장치부터 구성한다. 해당
Runtime이 여러 장치를 새 EP API로 노출하지 않으면 `device_id`가 다른
`OnnxProvider` 구성들을 명시하여 여러 GPU를 등록한다. GPU·NPU SDK 및
드라이버를 iiDecision이 대신 설치하지 않는다. CPU 전용 ONNX Runtime 패키지에
CUDA·QNN 같은 이름을 지정하는 것만으로 해당 가속기를 사용할 수 없다.

[제공자 목록과 API](https://onnxruntime.ai/docs/execution-providers/),
[플러그인 EP 사용 방법](https://onnxruntime.ai/docs/execution-providers/plugin-ep-libraries/usage.html).

세션에는 `session.disable_cpu_ep_fallback=1`과
`session.record_ep_graph_assignment_info=1`을 설정한다. 각 그래프가 선택한
제공자에 배정되었는지 확인하고, 그래프 배정 정보를 얻지 못하거나 다른 제공자에
배정되면 성공으로 보고하지 않는다. 완성된 세션을 형상별로 다시 사용하며,
메모리 증가를 제한하기 위해 각 어댑터는 마지막 형상의 세션 하나만 보관한다.

CoreML은 기본적으로 `MLProgram`, `MLComputeUnits=ALL`, 정적 입력 형상을
사용한다. `CPUAndGPU`·`CPUAndNeuralEngine` 구성을 별도로 등록할 수 있다.
이는 CoreML에 사용 가능한 계산 장치를 지정하는 설정이며, 실제 ANE 배치를
보장하는 설정은 아니다. CoreML·OpenVINO·QNN처럼 내부에서 장치를 관리하는
제공자는 `DeviceKind::Managed`로 표시한다. `execution.cpu_rows=0`은 iiDecision
및 ORT CPU 제공자의 대체 실행이 없었다는 뜻이며, 관리형 제공자 내부 CPU 작업이
없었다는 뜻은 아니다. ANE·개별 NPU 사용 여부는 제공자의 장치 프로파일로
추가 확인해야 한다. [CoreML EP 옵션](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html).

WebGPU의 기본 레이아웃은 이 그래프에 맞게 NCHW로 지정한다. 레이아웃에
민감한 연산은 없으며, ORT 1.30의 임시 NHWC 배정 단계에서 최종 배정 기록이
누락되는 문제를 피한다. WebGPU가 내부에서 사용하는 D3D12·Vulkan·Metal
장치 경로는 해당 Runtime의 Dawn 구현이 결정한다.
[네이티브 WebGPU EP](https://onnxruntime.ai/docs/execution-providers/WebGPU-ExecutionProvider.html).

## 선택, 동시 실행과 실패 처리

```cpp
iiDecision::AccelerationOptions options;
options.mode = iiDecision::ExecutionMode::RequireAccelerators;
options.backend_ids = {"metal/0", "mlx/0"};
auto hardware = std::make_shared<iiDecision::EvidenceMatcher>(options);
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

제공자 옵션 및 플러그인은 다음과 같이 구성한다.

```cpp
iiDecision::AccelerationOptions options;
options.onnx_providers = {
    {"CUDAExecutionProvider", {{"device_id", "0"}}},
    {"CUDAExecutionProvider", {{"device_id", "1"}}},
    {"CoreMLExecutionProvider", {{"MLComputeUnits", "CPUAndGPU"}}},
    {"CoreMLExecutionProvider", {{"MLComputeUnits", "CPUAndNeuralEngine"}}}
};
// 실제 설치한 플러그인의 등록 이름과 절대 경로를 지정한다.
// options.onnx_plugins = {{"vendor_ep", "/path/to/vendor_ep.so"}};
iiDecision::EvidenceMatcher hardware(options);
```

## 빌드와 의존성

각 옵션 `IIDECISION_METAL`·`IIDECISION_MLX`·`IIDECISION_OPENCL`·
`IIDECISION_VULKAN`·`IIDECISION_ONNX`는 `AUTO`·`ON`·`OFF`를 지원한다.
`AUTO`에서 SDK가 없으면 그 경로만 제외하며, `ON`은 SDK가 없으면 구성 오류가
된다. 가속 SDK가 있는 설치 패키지의 소비자도 동일 SDK의 라이브러리를 찾을 수
있어야 한다. ONNX Runtime 동적 라이브러리와 제공자 플러그인·벤더 라이브러리는
플랫폼의 loader 검색 경로에 배치한다. iiDecision은 이를 설치 패키지에 복제하지 않는다.
설치된 CMake 패키지는 네이티브 SDK의 링크 대상만 소비자에게 전달하여 loader
경로를 구성한다. 벤더 헤더와 컴파일 옵션은 공개 C++ 사용 계약으로 전달하지 않는다.

```sh
# MLX C++ 설치의 CMake 디렉터리와 ONNX Runtime C/C++ SDK 루트이다.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DMLX_DIR=/path/to/mlx/share/cmake/MLX \
  -DONNXRuntime_ROOT=/path/to/onnxruntime \
  -DIIDECISION_MLX=ON -DIIDECISION_ONNX=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

`tools/fetch_onnx.py`는 공식 1.30.0 무료 C/C++ SDK를 `build/dependencies/`에
받고 URL·SHA-256을 기록한다. Python 3.12 이상으로 실행한다. MLX는 공식 C++
설치 또는 헤더·라이브러리·CMake 설정이 들어 있는 SDK를 사용한다. Python
인터프리터에서 추론을 실행하지 않는다. OpenCL·Vulkan은 플랫폼 SDK를 사용한다.
Vulkan 커널의 SPIR-V는 소스와 함께 보관하며 수정 후 공식 glslang으로 재생성한다.

```sh
python3 tools/compile_shader.py /path/to/glslang
```

다른 SDK의 헤더를 전역 `CPATH`로 주입하는 환경에서는 해당 경로를 제거한
빌드 환경에서 SDK 헤더의 일치를 검증한다. 코어가 상위 제품 SDK를 참조하지 않는다.

## 검증 범위

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
