# 문서 검토와 설계 결정

## 2026-10-23 근거 평가와 행동 정책

[Build a Jev-powered Agentic RAG](https://blog.dailydoseofds.com/p/build-a-jev-powered-agentic-rag)의
본문을 읽고 관련성과 답변 가능성의 구분, 매체별 임계값, 동일 페이지 검색 결과의
병합, 1회 확장 검색 후 사람에게 이관하는 흐름과 판단 기록을 채택하였다.
글은 Jev를 개념으로 설명하고 실제 예제에는 Liquid AI d1-3B를 사용한다.
제시된 성능 수치는 해당 글의 결과이며 iiDecision의 성능 증거가 아니다.

[TypeSafe Noul](https://docs.typesafe.ai/primitives/noul)과
[Introduction](https://docs.typesafe.ai/introduction)을 다시 확인하였다. 각 조건은
독립된 typed 질문으로 묻고 코드를 통해 결합한다. Noul의 `P(true)`는 yes/no
판단의 확률이며 분포 집중도나 관련성의 정도와 혼용하지 않는다.

iiDecision에는 `RagDecisionGate`와 기존 Laya를 사용하는 텍스트 채점기를 추가한다.
정밀 확률 추론과 학습 모델의 의미를 보존하고 외부 서비스·에이전트 프레임워크를
도입하지 않는다. 문단별 답변 가능성을 통과해야 인용을 합치므로 여러 실패 후보가
서로 부족한 점수를 보완하는 오류를 방지한다. 구체적인 정책과 검증 범위는
[AgenticRag.md](AgenticRag.md)와 [Verification.md](Verification.md)에 있다.

## 2026-10-01 초기 확률 코어

2026-10-01 공식 문서를 검토하였다. 아래 표의 설계 결정은 iiDecision을 위한
판단이며 해당 도구의 전체 구현이나 모델을 복제한다는 의미가 아니다.

| 공식 자료 | 확인한 개념 | iiDecision의 적용 |
| --- | --- | --- |
|[Jev Introduction](https://docs.typesafe.ai/introduction), [Choice](https://docs.typesafe.ai/primitives/choice)| 타입이 정해진 질문과 모든 후보의 확률을 반환한다. | 최소 Decision 계약과 전체 분포 조회를 함께 제공한다. |
|[Jev Confidence](https://docs.typesafe.ai/confidence)| confidence는 분포에서 계산한 통계이며 전체 분포를 별도로 제공한다. | 엔트로피와 후보 확률을 명시하고 임의의 confidence 공식을 Jev 공식처럼 취급하지 않는다. |
|[Jev jaggedness](https://docs.typesafe.ai/model-jaggedness/jev-1.13)| 수치 정밀도와 계산의 한계를 설명하며 수학은 코드에서 처리하도록 권한다. | 수치 연산을 C++에서 수행하고 독립적인 고정밀 기준값으로 검증한다. |
|[pgmpy VariableElimination](https://pgmpy.org/api/generated/inference/pgmpy.inference.VariableElimination.html)| 관측 증거를 조건으로 결합·주변 확률을 조회한다. | 첫 모델은 유한 결합 상태의 전수 합산으로 조건부 주변 확률을 구한다. 그래프 실행기는 도입하지 않는다. |
|[pyAgrum 영향 다이어그램 추론](https://pyagrum.readthedocs.io/en/2.2.1/infdiagInference.html)| 증거 기반 추론과 최대 기대 효용을 구분한다. | 최대 확률 선택을 제공하며 효용 최적화와 동일하다고 주장하지 않는다. 효용 정책은 실제 요구가 확인된 뒤 추가한다. |
|[scikit-learn Calibration](https://scikit-learn.org/stable/modules/calibration.html)| Brier·log loss는 calibration만 측정하지 않는다. Reliability diagram은 예측 확률과 관측 빈도를 비교한다. | 정확도, log loss, multiclass Brier, reliability bins와 ECE를 함께 보고한다. |
|[득점자 검사](https://inspect.aisi.org.uk/scorers.html), [평가 로그](https://inspect.aisi.org.uk/eval-logs.html)| 사례별 정답과 채점을 분리하고 실행 설정·결과를 보존한다. | 네이티브 채점 함수와 재현 가능한 Decimal 기준값 검증 기록을 제공한다. Inspect 런타임 의존성은 추가하지 않는다. |

속도보다 정밀한 계산이 제품 목적이다. 이번 범위는 결정론적인 유한 상태 모델,
수치 안정성, 전체 확률과 불확실성, 독립 기준값 검증이다. 학습된 자연어 판단 모델,
LLM 호출, Agent·Planner·Memory 프레임워크는 이 요구의 구현 대상이 아니다.
