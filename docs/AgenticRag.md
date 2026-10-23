# 근거 평가와 답변 허용 정책

2026-10-23 [Build a Jev-powered Agentic RAG](https://blog.dailydoseofds.com/p/build-a-jev-powered-agentic-rag)를
검토하여 근거를 채점하는 모델과 그 결과를 행동으로 변환하는 정책을 분리하였다.
원문의 실제 예제는 Liquid AI d1-3B와 외부 서비스를 사용한다. iiDecision은 이미
제공하는 로컬 Laya를 텍스트 채점에 연결하고 C++23 정책을 추가한다.
Jev·d1·임베딩 모델을 새로 이식하거나 서비스에 접속하는 구현은 아니다.

## 판단 흐름

`검색·채점 → RagDecisionGate → Answer | RetrySearch | Escalate`이다.

- `relevance`는 질문과 관련 있다는 판단이고 `answerability`는 해당 근거만으로
  답변할 수 있다는 판단이다. 두 값 모두 유한한 `[0,1]` 값이어야 한다.
- 텍스트와 이미지에 각각 `RagThresholds`를 적용한다. 한 후보가 두 임계값을
  **모두** 충족하고 `complete=true`일 때만 답변 근거로 포함한다.
- 임계값은 `>=`이며 반올림·epsilon 보정 없이 `long double`로 비교한다.
  기본값 관련성 `0.7`, 답변 가능성 `0.8`은 설정 시작값이며 검증된 정확도나
  운영 권장값이 아니다. 별도의 정답 데이터로 보정하여야 한다.
- 통과한 후보는 답변 가능성 내림차순, 관련성 내림차순으로 정렬한다.
  동률은 입력 순서를 유지한다. `(document_id,page)`가 같은 후보는 하나의
  인용 페이지로 묶고 `max_pages`개의 페이지를 선택한다. 동일 페이지 안의
  통과 후보 ID는 입력 순서를 유지한다.
- 한 후보의 높은 관련성과 다른 후보의 높은 답변 가능성을 합치지 않는다.
  중복 근거의 점수를 더하거나 곱하지 않으며 독립성·결합 확률을 가정하지 않는다.
  여러 불완전한 문단을 종합해야 하는 질문은 종합 근거를 평가할 별도 채점이 필요하다.
- 최초 검색에서 통과 후보가 없으면 `RetrySearch`, 확장 검색에서도 없으면
  `Escalate`이다. 하나라도 선택되면 `Answer`이다.

`evaluate`는 개별 검색 회차를 평가하는 순수 조회이다. `run`은 호출자가 제공한
검색·채점 콜백을 `Initial`로 호출하고 필요한 경우에만 `Expanded`로 한 번 더
호출한다. 최대 두 번이며 최종 행동은 `Answer` 또는 `Escalate`이다.
호출자는 `Expanded`에서 실제로 검색 범위나 후보 수를 늘려야 한다.
정책 인스턴스는 한 실행 동안 변경되지 않는다. 새 설정을 읽으면 새 gate를
생성하므로 SDK를 다시 컴파일할 필요 없이 임계값을 교체할 수 있다.

```cpp
#include <iiDecision/RagDecisionGate.h>

iiDecision::RagPolicy policy;
policy.revision = "support-v2";
policy.text = {0.75L, 0.9L};
policy.image = {0.7L, 0.85L};
const iiDecision::RagDecisionGate gate(policy);
const auto run = gate.run([&](iiDecision::RagAttempt attempt) {
    // 애플리케이션이 검색과 해당 매체를 지원하는 모델의 채점을 수행한다.
    return retrieveAndScore(attempt);
});
const auto& final = run.attempts.back();
// final.action == Answer인 경우 final.pages의 근거만 생성 모델에 전달한다.
// Escalate이면 run.attempts를 검토 업무에 첨부한다.
```

실행 가능한 고정 점수 예제는 [examples/rag.cpp](../examples/rag.cpp)이다.

```sh
./build/iiDecisionRagExample
```

## Laya 텍스트 근거 채점

`LayaEvidenceScorer`는 질문과 각 문단을 JSON 문자열 값으로 구분하여 상태로
전달한다. 문단마다 관련성과 답변 가능성을 각각 묻는 Noul 질문 두 개를 하나의
로컬 ONNX forward pass로 처리한다. 다른 문단을 독립적으로 채점하므로 후보를
추가해도 앞선 문단의 상태에 텍스트를 덧붙이지 않는다.

```cpp
#include <iiDecision/LayaEvidenceScorer.h>
#include <array>

iiDecision::LayaDecisionMaker maker("build/models/laya");
iiDecision::LayaEvidenceScorer scorer(maker);
const std::array passages{
    iiDecision::RagTextEvidence{"refund", "handbook", 17,
        "Refunds may be requested within 14 days of purchase."}
};
const auto scores = scorer.score("How long is the refund window?", passages);
const auto report = iiDecision::RagDecisionGate{}.evaluate(scores.evidence);
// scores.traces에는 각 질문의 전체 분포, 토큰 수, 실행 진단이 보존된다.
```

`noul`인 `P(true)`를 점수로 사용한다. `confidence`, `answer_confidence`나
보조 행동 헤드 `act_probability`를 답변 가능성으로 대체하지 않는다.
Noul의 의미는 [TypeSafe 공식 문서](https://docs.typesafe.ai/primitives/noul)에서
확인한 개념이며, 실제 추론은 [Laya 계약](Laya.md)을 따른다.

한 질문에서라도 상태 또는 질문·판정 기준 토큰이 잘리면 `complete=false`이다.
`state_tokens_dropped`와 `head_tokens_dropped`를 모두 확인한다. 높은 점수가 나와도
gate는 `IncompleteEvidence`로 제외한다. 긴 문서는 호출자가 더 작은 문단으로
검색하거나 모델 토큰 예산을 적절히 설정해야 한다. 문서와 페이지 식별자는
추론 입력에 넣지 않고 결과에서 그대로 보존한다.

maker가 scorer보다 오래 살아 있어야 한다. 두 객체 모두 동시 const 조회를
지원한다. 잘못된 입력·모델 로딩·추론 실패는 예외로 전달되며 첫 후보 선택이나
답변 허용으로 우회하지 않는다. Laya가 빌드에서 제외되어도 gate는 독립적으로
사용할 수 있다. 채점기 사용에는 Laya 백엔드와 준비된 로컬 모델이 필요하다.

```sh
./build/iiDecisionRagExample build/models/laya
```

현재 Laya 채점기는 텍스트 전용이다. 이미지 점수는 실제 이미지를 처리하는
별도 모델이 `RagEvidence`로 제공한다. 이미지 파일명·OCR 문자열을 받았다는
이유만으로 시각 추론을 실행했다고 간주하지 않는다.

Laya와 ONNX 가속 백엔드는 환경 생성 후 ONNX Runtime telemetry를 비활성화한다.
로컬 추론에는 telemetry가 필요하지 않으며, 검증 중 관측한 ORT 1.30 macOS의
telemetry 종료 스레드 충돌을 피하도록 구성하였다. 이는 모델 출력을 변경하지 않는다.

## 감사와 인용 계약

`RagDecisionReport`는 적용한 정책 전체와 리비전, 회차, 최종 행동, 모든 후보의
원본 점수·완전성·제외 사유를 소유한다. 제외 사유는 `LowRelevance`,
`LowAnswerability`, `IncompleteEvidence`, `PageLimit`이며 한 후보에 여러 사유가
있을 수 있다. `pages`는 최종 인용 대상과 통과한 근거 ID, 순위를 결정한
`best_evidence_id`를 보존한다. `RagRunReport`에는 최초 검색의 거절 기록도 남는다.
호출자는 보고서를 자기 저장소에 기록할 수 있다. SDK는 로그 파일을 임의로 쓰지 않는다.

근거 ID는 회차 내에서 고유하고 공백 문자열이 아니어야 한다. 문서 ID도 공백이
아니어야 하며 페이지 번호는 1부터 시작한다. ID·점수·열거형·정책이 잘못되면
`std::invalid_argument`이다. 문서 ID는 테넌트 내부에서 유일하게 지정하고 검색
권한과 데이터 격리는 호출자가 보장한다. 빈 검색 결과는 정상적인 재검색·이관
대상이다. 콜백 예외는 호출자에게 전파되며 완료된 정상 판단으로 기록되지 않는다.

답변 생성, 임베딩, 벡터 저장소, PDF 렌더링, 티켓 생성, 이메일 발송과 학습 데이터
갱신은 소비 애플리케이션의 책임이다. `Answer`는 공급한 점수와 정책을 통과했다는
뜻이며 사실성이나 실세계 정확도의 보장이 아니다. 원문의 평가 결과를 iiDecision의
정확도 수치로 사용하지 않는다.
