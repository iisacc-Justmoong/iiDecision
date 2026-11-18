# iiDecision 에이전트 지침

의사결정 라이브러리는 다른 SDK 모듈을 찾거나 링크하지 않는다. Metal·Vulkan
가속은 독립 소비자 예제의 `IEvidenceBackend` 어댑터를 앱에서 주입한다.
정밀도 계약과 장치·버퍼 재사용 테스트를 보존한다.
빌드 산출물은 `build/`에 두며 테스트 임시 보고서와 보존된 의존 자료는 `resources/`에 둔다. 다른 프로젝트가 사용하는 기존 경로와 사용자 데이터를 보존한다.

## 라이선스

이 저장소는 iiSDK 모듈 iiDecision이며 Apache License 2.0이다. 자사 코드, 문서, 도구, 테스트가 포함된다. 저작권 표시는 `Copyright (c) 2026 iisacc`이고 고지는 루트 `NOTICE`에 있다. 사람용 한국어 설명은 [docs/License.md](docs/License.md)이다.

`Product` 아래의 iisacc 제품은 GNU Affero General Public License 3.0 이후 버전(AGPL-3.0-or-later)이다.

제3자와 벤더 코드는 원래 라이선스를 유지한다. 문서 재편 중에도 그 라이선스와 외부 원본은 보존한다.

<!-- workspace-docs:agent:start -->
## 문서 구조와 에이전트 탐색

- 프로젝트 루트의 `README.md`는 사람용 영문 진입 문서이며 `AGENT.md`는 에이전트 진입점이다.
- 하위 인간용 Markdown과 구조화된 agent JSON을 함께 유지한다. Markdown이 원본이며 JSON은 원문 해시와 줄 범위를 가진 파생 문서이다.
- 먼저 [docs/agent-index.json](docs/agent-index.json)에서 관련 문서와 JSON 투영을 찾는다. 원문 SHA-256이 다르면 현재 Markdown을 읽고 투영을 갱신한다.
- JSON의 규칙 후보는 검색용 어휘 일치 결과이다. 과거 기록·예제·실패 상태를 현재 지시나 검증 완료로 자동 해석하지 않는다.
- 기존 프로젝트별 규칙, 동결된 증거, 라이선스, 외부 원본, 사용자 데이터와 다른 작업의 변경을 보존한다.
- 이 문서 재편 지침은 과거 작업의 기능 구현·배포를 자동 재개하라는 요청이 아니다. 현재 사용자의 작업 범위를 우선한다.
- 문서를 수정하면 JSON 쌍·색인을 함께 갱신하고 검증한다. 기존 소스 변경에 대한 빌드·테스트 규칙은 계속 따른다.

Workspace에서 이 프로젝트의 문서를 갱신하는 명령은 다음과 같다.

```sh
python3 ../../Engineering/WorkspaceDocumentation/App/workspace_docs.py --workspace ../.. sync --project SDK/iiDecision
```
<!-- workspace-docs:agent:end -->
