# Laya 모델 백엔드 폐기

ONNX 기반 Laya 실행, 변환·다운로드 도구와 토크나이저 빌드를 제거하였다.
`LayaDecisionMaker::available()`은 false이며 모델 생성자는 명시적 폐기 오류를
반환한다. 모델 파일을 읽거나 다른 의사결정 방식으로 암묵적으로 대체하지 않는다.

기존 공개 데이터 객체와 진입점은 실패 처리를 위해 보존한다. 실제 의사결정은
`IDecisionMaker`, `ExactDecisionMaker`, `FirstCandidateDecisionMaker`와
`IEvidenceBackend`를 통해 구현을 선택한다. 유한 상태의 확률 계산과 RAG 게이트는
이 폐기된 학습 모델과 독립적이다.

`iiDecision.Laya.Retired`는 가용성·요청 거절·백엔드 목록을 검사한다. 확률·RAG·
네이티브 가속은 별도 테스트로 검증한다. 과거 Laya의 확률 비교 결과를 현재 모델
지원이나 다른 엔진의 동등성 증거로 사용하지 않는다.
