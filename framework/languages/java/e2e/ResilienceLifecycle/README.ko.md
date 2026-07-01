# Java ResilienceLifecycle E2E

이 디렉터리는 공통 E2E Config 5 Resilience/lifecycle 시나리오를 Java framework public API로 검증한다.
구조는 `.NET` 기준 구현의 책임 분리를 따른다.

## 역할

- `Shared`: client와 server가 함께 쓰는 request, reply, evidence 타입과 환경 변수 helper.
- `Client`: HTTP driver process. framework runtime으로 뜨지 않고 provider/consumer process lifecycle을 제어한 뒤
  `Server/Consumer`의 scenario endpoint를 호출한다.
- `Server/Consumer`: discovery client와 channel client를 가진 framework participant. scenario traffic을 보내고
  registry topology를 조회한다.
- `Server/Registry`: embedded registry process.
- `Server/Provider`: provider process, framework handler, evidence/admin HTTP endpoint.

## 실행

```bash
./run_e2e.sh
```

runner는 Gradle `installDist`를 먼저 실행한 뒤 registry와 Client suite를 띄운다. provider와 consumer
role binary의 시작, 종료, 재시작은 Client support가 담당한다. 실행 로그는 `logs/<run-id>/` 아래에 남는다.

현재 Java 구현은 `feature-map.ko.md`에 적은 scenario만 완료로 본다. public API나 harness 제어가 더
필요한 항목은 gap으로 남긴다. `.NET Client/Scenarios/*.cs`에 대응하는 Java Client scenario 파일은
있으며, 완료되지 않은 scenario 파일은 선택되면 gap 사유와 함께 실패한다.
