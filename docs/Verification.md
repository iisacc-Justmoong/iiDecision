# iiDecision 0.0.4 검증

## 0.0.4 Laya 백엔드 검증

2026-10-03 macOS arm64에서 다음 결과를 확인하였다. 아래의 0.0.3 기록은
이전 가속 계층의 검증 이력이며 이번 Laya 그래프 실행과 구분한다.

| 구성 | 빌드와 테스트 | 설치 패키지 소비 테스트 |
| --- | --- | --- |
| 기존 가속 SDK 전체와 Laya를 포함한 정적 라이브러리 | 성공, 31/31 통과 | 기존 API와 Laya 2/2 통과 |
| ONNX와 Laya를 포함한 공유 라이브러리 | 성공, 27개 통과, 제외한 장치 4개 Skipped | 기존 API와 Laya 2/2 통과 |
| 가속 SDK와 Laya를 제외한 CPU 코어 | 성공, 20개 통과, 제외한 경로 11개 Skipped | 이번 구성에서 별도 설치 소비는 실행하지 않음 |

기존 독립 Decimal oracle 171개 사례도 통과하였다. Laya 테스트는 typed 출력,
옵션 순서·온도 bucket/clamp·단일 옵션·상태 잘림·literal mask·UTF-8,
잘못된 질문·패키지·special token·ONNX signature·비유한 logits의 거부,
동시 호출과 IDecisionMaker 연결을 확인한다. 테스트 전용 작은 ONNX 그래프를
사용하며 실제 모델 호환 증거는 다음 비교에서 얻는다.

`tests/laya_parity.py`는 공식 루트 체크포인트와 C++ 실행 파일을 직접 실행한다.
상태 6개, Choice·Score·Noul 응답 18개를 eager Agent·ONNXAgent와 비교하고
`build/laya-parity.json`을 기록한다. 반올림 전 ONNX 분포와 C++ 분포의 최대 차이는
`1.1102230246251565e-16`, 4자리 JSON 분포와의 최대 차이는
`4.5368811903423456e-05`이다. 선택 결과·confidence·기대값·토큰·잘림 수가
검증 기준을 통과하였다. 단일 선택지의 실제 모델 실행도 완료하였다.

`tools/export_laya.py`로 공식 체크포인트를 비양자화 float32 패키지로 내보내고
manifest의 파일 5개 SHA-256을 대조하였다. 원본 리비전, 재현 명령과 지원 범위는
[Laya 문서](Laya.md)에 있다. Linux·Windows·모바일 장치의 Laya 실행,
multilingual·typed-decisions 체크포인트와 GPU/NPU 그래프 실행은 이번 검증에
포함하지 않는다. CI에 Cargo 준비와 Laya 테스트를 연결하였으나 원격 CI 실행은
이번 로컬 결과의 근거로 사용하지 않는다.

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

0.0.3에서는 실행 계약·CPU 기준·여러 장치의 구간 분배·실패 처리·모델 통합
테스트를 먼저 작성하였다. 생성자와 실행 보고서가 없는 기존 구현에서 컴파일이
실패한 기록을 `build/acceleration-tdd-red.log`에 보존하였다. 이후 가속 구현을
연결하여 통과시켰다. 잘못된 행렬 형상, uint32 상위 비트, 비정상 마스크,
누락·중복 경로 ID, 가속기 실패, 미사용 장치의 호출 방지, 엄격 모드의 CPU
우회 금지 및 실패 구간만 CPU로 계산하는 동작을 검증한다.

## 빌드와 설치 패키지

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=OFF
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/acceleration-stage"
cmake -S tests/consumer -B build/consumer/build -DCMAKE_BUILD_TYPE=Release -DiiDecision_DIR="$PWD/build/acceleration-stage/lib/cmake/iiDecision"
cmake --build build/consumer/build --parallel 2
ctest --test-dir build/consumer/build --output-on-failure
```

별도 소비 프로젝트는 `find_package(iiDecision 0.0.4 EXACT CONFIG REQUIRED)`와
`iiDecision::iiDecision`을 사용하여 기존 계약, 가속된 확률 계산과 평가 API를
검증한다. 가속 SDK를 포함한 구성에서는 해당 SDK 경로도 CMake에 전달한다.
동일한 검증을 `BUILD_SHARED_LIBS=ON`과 `build/acceleration-shared-stage`에서도
수행한다. 공유 패키지가 SDK의 loader 경로를 소비자에게 전달하는지 확인한다.
검증 중 발견한 MoltenVK 로딩 실패를 이 연결 방식으로 수정하였다.

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
이번 검증의 구성별 기록은 `build/decimal-oracle-acceleration-static.json`과
`build/decimal-oracle-acceleration-shared.json`이다. 단일 구성 및 다중 구성
생성기에서 CMake cache 위치를 찾으며, 활성화한 가속 옵션과 SDK 경로도 기록한다.

## 실행 결과

2026-10-01 macOS arm64, Release 빌드에서 다음 결과를 확인하였다.
모든 빌드 및 임시 설치 산출물은 프로젝트의 `build/` 아래에 생성하였다.

| 구성 | 전체 빌드 | 단위 테스트 및 예제 | 설치 패키지 소비 테스트 |
| --- | --- | --- | --- |
| 모든 가속 SDK를 포함한 정적 라이브러리 | 성공 | 25/25 통과, Decimal 171/171 | 1/1 통과 |
| 모든 가속 SDK를 포함한 공유 라이브러리 | 성공 | 25/25 통과, Decimal 171/171 | 1/1 통과 |
| 가속 SDK를 모두 제외한 CPU 구성 | 성공 | 19개 통과, 하드웨어·ORT 6개 Skipped, Decimal 171/171 | 이번 구성에서는 별도 실행하지 않음 |
| iOS arm64, Metal 포함 | Release 빌드 성공 | 기기 런타임 테스트는 수행하지 않음 | 이번 구성에서는 별도 실행하지 않음 |

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

## 실제 하드웨어 실행

Apple M1 Max에서 다음 경로가 각각 엄격 모드로 완료되었다. 단순 장치 발견을
실행 성공으로 세지 않는다. 초기화 후 장치 커널·그래프를 실행하고 완료된 결과를
읽어 비교하였다. 전역 `CPATH`, `LIBRARY_PATH`, `DYLD_LIBRARY_PATH` 및
`DYLD_FALLBACK_LIBRARY_PATH`의 상위 SDK 의존을 제거하여 빌드·실행을 확인하였다.

| 경로 ID | 큰 uint32 ID 및 259행 마스크 | 확률·로그 확률·진단의 CPU 기준 비교 |
| --- | --- | --- |
| `metal/0` | 완전 일치 | 80/80 완전 일치 |
| `mlx/0` | 완전 일치 | 80/80 완전 일치 |
| `opencl/0/0` | 완전 일치 | 80/80 완전 일치 |
| `vulkan/0` | 완전 일치 | 80/80 완전 일치 |
| `onnx/CoreMLExecutionProvider/0` | 완전 일치 | 80/80 완전 일치 |
| `onnx/WebGpuExecutionProvider/0/device/0` | 완전 일치 | 80/80 완전 일치 |

각 80개 사례는 증거가 있는 76개 장치 실행과 빈 증거 4개 항등 연산을 포함한다.
상관관계, 매우 작은 증거 질량, 선형 확률 underflow, 입력 후보 순서의 동률을
검증하며 전체 확률 보고서의 수치를 기준 결과와 비교한다. 별도로 ONNX CPU
제공자의 등록·그래프 배정·실행도 검증하며, 이 결과는 GPU 실행 수에 넣지 않는다.

1,024개 상태를 여섯 경로에 동시에 분배한 별도 실행에서 CPU 대체 실행 없이
모든 경로가 완료되었다. 앞의 네 경로는 각각 171행, CoreML과 WebGPU는 각각
170행을 처리하였다. 같은 물리 GPU를 여러 API로 공유한 실행이며 여러 물리 GPU나
속도 개선의 증거로 해석하지 않는다.

네이티브 실행 기록과 소스·실행 파일·라이브러리 SHA-256은
`build/acceleration-static-audit.json`, `build/acceleration-shared-audit.json`에
보존한다. 재생성 명령은 다음과 같다.

```sh
python3 tools/record_acceleration.py build/iiDecisionAccelerationTests build/acceleration-audit.json build/libiiDecision.a
```

검증 SDK는 MLX C++ 0.32.2, ONNX Runtime 1.30.0, Apple OpenCL 1.2,
MoltenVK 1.4.2 및 현재 Xcode Metal SDK이다. MLX wheel의 CMake 설정에 들어 있는
이전 Xcode 프레임워크 검색 경로에 대해 linker 경고가 있으나, 이 라이브러리의
소스 컴파일 경고는 없고 실제 네이티브 실행 및 설치 소비 검증은 통과하였다.

CoreML 제공자 배정은 확인하였으나 ANE 배치는 확인하지 않았다. iOS는 크로스
빌드 증거이며 기기 실행 증거는 아니다. Windows·Linux·Android와 NVIDIA·AMD·
Intel·Qualcomm·Huawei·Rockchip·FPGA 등의 물리 장치 실행은 현재 환경에서
확인하지 않았다. 수동 GitHub Actions에는 데스크톱 빌드·테스트 및 Android·iOS
빌드를 정의하였으며 이번 작업에서 호스팅 실행을 수행하지 않았다.
