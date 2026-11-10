# 빌드와 테스트 리소스

기본 구성은 `build/`에 모듈과 테스트를 빌드하고 예제는 기본적으로 제외한다. 독립 소비자가 사용하는 외부 의존 자료와 보존할 모델 데이터는 `resources/dependencies/`, `resources/models/`에 둔다. Unix의 최상위 `build/` 구성은 존재하는 리소스에 대해 이전 `build/dependencies`, `build/models`, `build/laya-tools` 경로의 호환 링크를 만든다. 이미 존재하는 다른 파일이나 링크는 덮어쓰지 않는다. Windows에서는 자동 링크를 만들지 않으므로 리소스 경로를 직접 지정한다.

Unix의 `install.sh`는 `build/`를 사용하고 기본적으로 `~/.local/SDK/iiDecision`에 설치한다. `IIDECISION_INSTALL_PREFIX`로 목적지를 변경할 수 있다. 실제 장치 검사까지 수행하려면 `IIDECISION_VALIDATE_DEVICES=1`을 명시한다. 전체 CTest는 별도로 실행할 수 있다.

정밀도 oracle은 같은 171개 사례와 기존 오차 기준을 유지한다. 보고서는 iiFileProvider를 통해 테스트별 `resources/.t/` 작업 공간에 생성하고 정상 종료나 예외 후 정리한다. 테스트 도구는 `IIDECISION_TEST_PROVIDER_PYTHON_DIR`로 지정할 수 있는 iiFileProvider Python 어댑터를 사용하지만 iiDecision 공개 라이브러리는 다른 SDK 모듈에 의존하지 않는다. 테스트 프로세스가 강제 종료되면 정리 코드가 실행되지 않을 수 있으므로 남은 작업 공간은 사용 여부를 확인한 뒤 정리한다.

구성 기본값 검사는 일시적인 `build/production-default-probe`를 만들고 종료 시 정리한다. 다른 프로젝트의 캐시가 참조하는 자료와 사용자 데이터는 정리 전에 확인하고 필요한 경우 `resources/.recovery/`에 보존한다. 과거 `build/` 경로의 검증 기록은 현재 산출물을 의미하지 않는다.
