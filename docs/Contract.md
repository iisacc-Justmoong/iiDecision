# iiDecision 0.0.5 계약

소스는 `src/Decision/`, `src/Evaluation/`, `src/RAG/`, `src/Laya/`,
`src/Acceleration/`, `src/backends/`와 공용 수치 계산용 `src/Core/`로
구분한다. 인터페이스와 해당 구현은 함께 두며, 기존 `<iiDecision/...>` 공개
include 경로와 설치된 헤더는 유지한다.
설치 소비자 검사는 `IIDECISION_CONSUMER_CPU_ONLY=ON`으로 가속기 탐색 없이
공개 CMake 패키지와 판단 API를 검사할 수 있다.

의사결정 파이프라인은 `Context → IDecisionMaker → Decision`이다.

- `Candidate`는 호출자가 정한 `std::string id` 하나를 소유한다.
- `Context`는 범주형 관측값 `Observation { key, value }`의 목록을 소유한다.
  `Context{}`는 관측이 없는 입력이며 0.0.1의 호출 코드도 계속 컴파일된다.
- `Decision`은 선택한 후보의 `std::string candidate_id` 하나를 소유한다.
- `IDecisionMaker::decide(const Context&, std::span<const Candidate>)`는 입력 후보
  중 하나의 식별자를 반환한다. 후보는 하나 이상이어야 한다.
- `FirstCandidateDecisionMaker`는 Context를 사용하지 않고 첫 후보의 식별자를
  복사하여 반환한다. 후보가 없으면 `std::invalid_argument`를 던진다.

호출자는 호출 동안 Context와 후보 배열의 수명을 유지한다. 구현은 입력을
수정하거나 참조를 보관하지 않는다. 반환한 Decision은 입력 배열과 독립적으로
식별자를 소유한다. 식별자의 의미, 빈 문자열 허용 여부, 중복 식별자의 처리 정책은
호출자가 결정하며 첫 후보 선택 구현은 식별자 값을 검증하지 않는다.

소비자는 `IDecisionMaker&`로 판단을 요청하여 구현을 교체할 수 있다.

0.0.x는 개발 중인 계약이며 바이너리 ABI 호환을 보장하지 않는다. 보고서 구조나
생성자 계약이 바뀐 SDK를 교체할 때 소비 프로젝트도 해당 헤더로 다시 빌드한다.

## ExactDecisionMaker

- 생성자는 `JointState { candidate_id, observations, log_weight }` 목록을 소유한다.
  모델의 상태는 변경하지 않으며 `evaluate`는 const 조회이다.
  선택적으로 `std::shared_ptr<EvidenceMatcher>`를 전달하여 실행 경로를 바꾼다.
  생략하면 프로세스 내 기본 matcher를 공유하며 사용 가능한 가속기를 발견한다.
- 모든 상태는 같은 관측 키 집합을 사용한다. 키는 비어 있지 않아야 하며 각 상태와
  Context에서 한 번만 나타나야 한다. 값은 범주형 문자열이다.
- 동일한 후보·관측 조합은 한 번만 선언한다. 생략한 결합 상태의 질량은 0으로
  해석한다. 관측하지 않은 변수는 전수 합산으로 주변화한다.
- 로그 가중치는 자연로그 기준의 비정규화 질량이다. 유한값 또는 음의 무한대만
  허용하며 음의 무한대는 구조적 확률 0이다. 전체 질량은 양수여야 한다.
- 후보 ID는 비어 있지 않고 유일해야 하며, 호출 후보 목록은 모델의 전체 후보
  집합과 같아야 한다. 부분 후보 목록을 자동으로 재정규화하지 않는다.
- 결과 분포는 호출 후보 순서를 유지한다. 최대 로그 확률을 선택하며 동률은
  입력 순서의 첫 후보를 선택한다. 확률 0 후보도 결과에 남긴다.
- 잘못된 스키마, 중복, 알 수 없는 관측값·후보는 `std::invalid_argument`이다.
  알려진 값의 조합이 확률 0이면 `std::domain_error`이다. 수치 범위를 초과하는
  연산은 `std::overflow_error`이다. 실패 시 모델을 변경하거나 첫 후보로 대체하지 않는다.

`ProbabilityReport`는 Decision과 전체 분포, 로그 증거 확률, 엔트로피와 수치
진단값을 소유한다. 원본 입력의 수명과 독립적이다. 반환된 필드를 수정한 경우
평가 함수는 분포·로그 확률의 일관성을 다시 검증한다.

## 평가 하네스

`evaluatePredictions`는 고유한 사례 ID, 예측 분포, 정답 후보를 받아 사례별 점수와
집계 결과를 반환한다. 실제 예측을 생성하는 모델과 채점 함수를 분리한다.
정답과 선택 후보는 분포에 존재해야 한다. 잘못된 분포와 빈 데이터는
`std::invalid_argument`이다. 확률 0 정답의 log loss는 양의 무한대로 유지한다.

도메인별 행동 실행, LLM 호출과 효용 정책은 소비자가 담당한다. 코어에는
NPC·Task·Agent·Planner·Memory 타입을 추가하지 않는다.

## LayaDecisionMaker

공식 Laya 모델을 로컬 ONNX 그래프로 실행하는 교체 가능한 `IDecisionMaker`
구현이다. Context 관측값을 JSON 상태로 직렬화하고 호출 후보를 Choice 옵션으로
사용한다. 빈 Context도 유효하며 빈 키·중복 키와 빈 후보·중복 후보는 거부한다.
`evaluate(text,candidates)`는 같은 판단과 전체 학습 분포를 반환한다.

`predict(text,questions)`는 Choice·Score·Noul을 한 배치에서 처리한다. 반환값은
입력과 독립적으로 질문 ID·옵션 ID·분포·토큰 진단·실행 보고를 소유한다. 동일
인스턴스의 동시 호출을 허용한다. 모델 로딩 실패나 추론 실패를 기본 행동으로
대체하지 않는다. 원본 소스와 모델 패키지, 온도와 정밀도의 의미, 지원 범위는
[Laya 문서](Laya.md)에 정의한다.

## RAG 근거 정책

`RagDecisionGate`는 `IDecisionMaker`의 최소 계약을 변경하지 않는 독립 정책이다.
원본 점수의 정밀도를 유지하여 관련성과 답변 가능성을 각각 비교하며 텍스트·이미지
임계값을 구분한다. 통과한 근거는 문서·페이지 단위로 묶어 인용하고 재검색은 한 번으로
제한한다. 보고서는 입력과 독립적으로 모든 판단 근거를 소유한다.

`LayaEvidenceScorer`는 기존 maker를 참조하여 텍스트 문단의 Noul 두 질문을
한 번에 실행한다. maker의 수명은 scorer보다 길어야 한다. 상태·질문·판정 기준 토큰이 잘리면
완전하지 않은 근거로 표시하며 gate가 답변을 허용하지 않는다. 이미지 추론 능력을
추가하지 않는다. 상세한 입력·오류·정렬·재시도 계약은
[Agentic RAG 문서](AgenticRag.md)에 정의한다.
