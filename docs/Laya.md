# Laya 모델 백엔드

`LayaDecisionMaker`는 주어진 UTF-8 상태와 질문의 선택지를 로컬 모델에 입력하여
행동을 선택한다. `ExactDecisionMaker`의 전수 확률 계산과 별도의 구현이며,
기존 `Context`, `Candidate`, `Decision`, `IDecisionMaker` 계약을 변경하지 않는다.

## 근거 소스와 모델

- [공식 Laya 저장소](https://github.com/NandhaKishorM/laya), 커밋
  `fa9a2a7070b1789912a49ae24603bbfb1a78b001`, 패키지 버전 `0.3.24`이다.
- [공식 모델](https://huggingface.co/convaiinnovations/laya), 검증 대상 커밋
  `55cf4c4ebb4ebe31b2550e8bdf3bd21b99753851`이다.
- `laya/common.py`의 `render_options`, `build_head`, `build_sequence`,
  `temp_bucket`, `clamp_temperature`, confidence 계산을 C++로 옮겼다.
- `laya/onnx_agent.py`의 입력 collate와 typed 결과 해석,
  `scripts/export_onnx.py`의 모델 내보내기 계약을 사용한다.
- Apache 2.0 원문과 변경 고지는 `third_party/laya/`에 있다. 이 적용 범위가
  기존 iiDecision 전체 소스의 라이선스를 변경하는 것은 아니다.

## 빌드와 의존성

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DIIDECISION_ONNX=ON -DONNXRuntime_ROOT=<onnx-cpp-sdk> -DIIDECISION_LAYA=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

`IIDECISION_LAYA`는 `AUTO`, `ON`, `OFF`이다. `AUTO`는 ONNX Runtime C++ SDK와
Cargo가 발견된 네이티브 빌드에서 활성화한다. `ON`은 의존성이 없으면 구성에
실패한다. `OFF`에서도 공개 헤더와 기존 의사결정 구현은 사용할 수 있으며,
`LayaDecisionMaker::available()`은 false이고 생성자는 명시적으로 실패한다.

소비자 API, 시퀀스 구성, 결과 계산은 C++23이다. 모델 토크나이저의 BPE/Unigram,
Unicode 처리와 추가 토큰을 재현하기 위해 Hugging Face `tokenizers` 0.22.2의
Rust 네이티브 구현을 작은 비공개 C ABI로 연결한다. Cargo.lock으로 의존성을
고정하며 산출물은 `build/laya-rust/`에 있다. 설치 패키지는 이 정적 아카이브를
함께 제공하므로 소비자가 Rust 도구를 실행할 필요는 없다. Qt와 Python은
추론의 실행 의존성이 아니다. 최초 빌드에는 Cargo 의존성 다운로드가 필요하다.

교차 빌드는 해당 ONNX SDK와 Rust 타깃을 준비하고 `IIDECISION_LAYA_RUST_TARGET`을
명시해야 한다. 미지정 교차 빌드에서 AUTO는 Laya를 끄고 ON은 실패한다.
플랫폼별 링크 구성이 물리 장치에서의 실행을 보장하지는 않는다.

## 모델 패키지 준비

런타임은 로컬 경로만 받으며 다운로드와 양자화를 수행하지 않는다.
공식 `model.safetensors`를 그대로 모델 그래프로 내보낸 다음 사용한다.

```sh
python -m pip install 'laya==0.3.24' onnx onnxscript onnxruntime
hf download convaiinnovations/laya \
  --revision 55cf4c4ebb4ebe31b2550e8bdf3bd21b99753851 \
  --include model.safetensors rl_agent_config.json 'encoder/*' 'tokenizer/*' \
  --local-dir build/models/laya-source
python tools/export_laya.py --checkpoint build/models/laya-source \
  --output build/models/laya \
  --revision 55cf4c4ebb4ebe31b2550e8bdf3bd21b99753851
build/iiDecisionLayaExample build/models/laya
```

패키지는 `laya.onnx`, ONNX 외부 가중치 파일이 있을 경우 그 파일,
`rl_agent_config.json`, `tokenizer/tokenizer.json`, `tokenizer/tokenizer_config.json`을
함께 보관한다. `iidecision_laya.json`은 준비 도구가 기록한 버전·원본 리비전·SHA-256
목록이다. 런타임이 이 manifest를 자동으로 검증하지는 않는다. 배포자는 모델
파일을 함께 운반하고 manifest와 실제 배포 파일의 해시를 대조해야 한다.

## 호출 계약

```cpp
iiDecision::LayaDecisionMaker maker("local-laya-package");
const std::array questions{
    iiDecision::LayaQuestion{"route", "Choose the next action.",
        iiDecision::LayaQuestionType::Choice,
        {{"refund", "Issue a refund"}, {"support", "Provide technical support"}}}
};
const auto answers = maker.predict("The customer reports an application crash.", questions);
```

`predict`는 상태 하나의 모든 질문을 배치하여 ONNX forward pass 한 번으로 실행한다.
질문 ID는 비어 있지 않고 고유해야 하며 instructions도 비어 있지 않아야 한다.
옵션 ID는 비어 있지 않고 고유해야 한다. 질문·옵션 순서와 확률 분포 순서를 보존한다.
동률은 첫 옵션을 선택한다. 상태 문자열은 UTF-8이며 비어 있을 수 있다.

| 질문 | 옵션 | 결과 |
| --- | --- | --- |
| Choice | ID와 설명 | 최고 확률의 ID와 전체 분포 |
| Score | ID `0`, `1`, ...와 단계 설명 | 전체 분포와 단계 인덱스의 기대값 `score` |
| Noul | `false`, `true` 순서와 각각의 설명 | `true` 확률 `noul`과 전체 분포 |

구조화된 criteria는 소비자가 JSON 문자열로 렌더링하여 description에 전달할 수 있다.
자동 체크포인트 라우터, 언어별 온도 override, 옵션 permutation, 대화 목록의 왼쪽
절단, 긴 문서 sliding window와 Python hook API는 이 C++ 계약에 포함하지 않는다.
다른 Laya 체크포인트도 동일한 ONNX·토크나이저 계약의 패키지는 로드할 수 있으나,
각 체크포인트에 대한 수치 비교 검증은 별도로 필요하다. 루트 모델의 언어 범위는
영어이다. 한국어 등 다국어 업무는 multilingual 체크포인트 검증을 거쳐야 한다.

`evaluate(text, candidates)`는 후보 ID만으로 Choice 질문을 만든다. 질문 문구는
생성자의 `LayaOptions::instructions`로 지정한다. `decide(Context, candidates)`는
관측 키와 값을 삽입 순서의 JSON 객체로 직렬화하여 같은 경로로 실행한다.
빈 Context는 `{}`이며, 빈 관측 키와 중복 키는 invalid_argument이다.

## 토큰·확률·실행 보고

시퀀스는 공식 구현과 같이 `[CLS] type question: instructions [SEP] [MASK] option ...
[SEP] state [SEP]`이다. 토크나이저의 실제 special token ID를 사용하며, 입력에
포함된 literal mask 문자열은 공백으로 치환한다. 설명은 먼저 48토큰으로 제한하고
head 예산이 부족하면 공식 per-option 제한과 instructions 제한을 적용한다.
기본 길이는 config의 `max_len`과 `head_max_len`이다. 생성자의 0이 아닌 설정으로
바꿀 수 있다. 마커가 전체 길이에 들어가지 않으면 오류이며 첫 후보로 대체하지 않는다.

`state_tokens`, `state_tokens_dropped`는 모델이 읽은 상태의 범위를 보여준다.
`options_distinct`가 옵션 수보다 작으면 잘림 이후 선택지의 토큰 표현이 겹친다.
각 질문의 `input_tokens`에는 패딩을 제외한 실제 입력 토큰 수가 기록된다.

온도는 질문 종류와 옵션 수 bucket에 맞는 `temperature_by_options`를 우선하고
`temperature`를 기본값으로 사용한다. 공식 clamp 범위 `[0.5,5.0]`을 적용한다.
모델 forward pass는 float32 그래프이며 softmax와 로그 확률은 C++ `long double`로
계산한다. 공개 분포는 공식 Python JSON의 소수 4자리 반올림 이전 값이다.
이 값은 학습된 모델의 판단 분포이며 ExactDecisionMaker의 전수 조건부 확률과
동일한 의미가 아니다. float32 logits를 이후 계산에서 고정밀로 다룬다고 모델의
정확도나 calibration이 높아지는 것은 아니다.

Choice와 Score의 `confidence`는 `1-H(p)/log(k)`이며 `answer_confidence`는 `max(p)`이다.
Noul confidence는 공식 구현과 같이 `max(p)`이다. `act_probability`는 별도의
auxiliary head 첫 행동 확률이며 행동 실행 기준으로 사용하지 않는다. 공식 모델
카드도 기본 모델의 calibration과 해당 head의 한계를 설명하므로, 실제 업무의
정답 데이터로 평가·보정한 후 정책을 정해야 한다.

현재 연결한 실행 경로는 ONNX Runtime CPU이며 `laya/onnx/CPUExecutionProvider`와
완료 행 수를 기록한다. 한 호출의 각 답에는 같은 배치 실행 보고를 붙이므로,
전체 forward pass 수를 계산할 때 답마다 이 보고를 중복 합산하지 않는다.
공식 ONNX 내보내기가 action head의 topk(2) 경로를 trace하므로, 단일 선택지
배치에도 가려진 두 번째 마커 슬롯을 추가하여 eager 구현과 같은 의미를 유지한다.
기존 evidence accelerator의 GPU/NPU 실행 증거를 Laya
그래프 실행 증거로 사용하지 않는다. 동일 인스턴스의 동시 predict를 지원하며
세션과 tokenizer를 조회용으로 사용한다. 손상된 모델, 잘못된 입력·출력 rank/type,
비유한 logits는 오류이고 다른 모델이나 임의 행동으로 대체하지 않는다.

## 검증 재현

```sh
python tests/laya_parity.py --checkpoint build/models/laya-source \
  --package build/models/laya --driver build/iiDecisionLayaParityDriver \
  --output build/laya-parity.json
build/iiDecisionLayaParityDriver build/models/laya single
```

2026-10-03 macOS arm64에서 공식 루트 체크포인트로 상태 6개 × 질문 3종의
18개 응답을 공식 eager Agent·ONNXAgent와 비교하였다. 행동 선택, 분포,
confidence, Score 기대값, Noul 확률과 토큰·잘림 정보를 확인하였다. 빈 상태,
literal mask, Unicode와 전체 입력 길이를 초과하는 상태를 포함한다.
반올림 전 공식 ONNX 확률과 C++ 확률의 최대 절대 차이는
`1.1102230246251565e-16`이며, 4자리 공개 JSON과의 차이는
`4.5368811903423456e-05`이다. eager·ONNX 공개 분포 차이는 이 사례들에서 0이다.
실제 체크포인트의 단일 선택지 실행도 확률 1로 완료하였다.
이는 모델 및 입출력 호환 검증이며 업무 정확도·속도·다국어 성능 평가가 아니다.
