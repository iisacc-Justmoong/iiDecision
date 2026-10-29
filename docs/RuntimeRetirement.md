# 폐기한 모델 런타임

2026-10-27부터 ONNX·ONNX Runtime을 빌드·모델 실행·설치·배포 경로에서 제거한다.
선택적 설치나 자동 다운로드 경로도 제공하지 않는다. 과거 검증 기록은 현재 지원
선언이 아니며, 해당 모델을 지원하는 것으로 간주하지 않는다.

ONNX 가속 공급자, 모델 다운로드 도구와 학습된 Laya 구현을 제거한다.
`LayaDecisionMaker::available()`은 `false`이며 생성자는 폐기 상태를 명시한다.
기존 의사결정·증거 매칭·확률 계산 API와 지원되는 네이티브 가속 공급자는 유지한다.

`iiDecision.Laya.Retired`는 가용성, 명시적 실패와 가속 공급자 목록을 확인한다.

iiDecision·iiLocalDiffusion·iiVoiceOver의 설치 갱신은 해당 SDK 접두사에 남은 구형
ONNX 공유 라이브러리를 제거한다. 제품 공통 배포 정책은 ONNX 런타임, 기존 Piper
바이너리와 OpenCV DNN의 재포함을 거부한다. 폐기한 모델 기능이 필요하면 지원되는
네이티브 API와 교체 가능한 iiSDK 인터페이스에 별도 구현을 연결해야 한다.
