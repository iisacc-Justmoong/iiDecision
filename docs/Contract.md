# iiDecision 0.0.1 계약

의사결정 파이프라인은 `Context → IDecisionMaker → Decision`이다.

- `Candidate`는 호출자가 정한 `std::string id` 하나를 소유한다.
- `Context`는 최초 구현에서 비어 있다.
- `Decision`은 선택한 후보의 `std::string candidate_id` 하나를 소유한다.
- `IDecisionMaker::decide(const Context&, std::span<const Candidate>)`는 입력 후보
  중 하나의 식별자를 반환한다. 후보는 하나 이상이어야 한다.
- `FirstCandidateDecisionMaker`는 Context를 사용하지 않고 첫 후보의 식별자를
  복사하여 반환한다. 후보가 없으면 `std::invalid_argument`를 던진다.

호출자는 호출 동안 Context와 후보 배열의 수명을 유지한다. 구현은 입력을
수정하거나 참조를 보관하지 않는다. 반환한 Decision은 입력 배열과 독립적으로
식별자를 소유한다. 식별자의 의미, 빈 문자열 허용 여부, 중복 식별자의 처리 정책은
호출자가 결정하며 첫 후보 선택 구현은 식별자 값을 검증하지 않는다.

소비자는 `IDecisionMaker&`로 판단을 요청하여 구현을 교체할 수 있다. 0.0.1에는
판단 구현 하나만 존재하며, 점수, 확률, 학습, LLM 연동 또는 특정 도메인의
NPC·Task 타입은 추가하지 않는다. 다음 구현은 실제 적용처의 요구가 확인된 뒤
추가한다.
