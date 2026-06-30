# Java ResilienceLifecycle E2E

이 디렉터리는 공통 E2E Config 5 Resilience/lifecycle 시나리오를 Java framework public API로 검증한다.
구조는 `.NET` 기준 구현의 책임 분리를 따른다.

## 역할

- `Shared`: client와 server가 함께 쓰는 request, reply, evidence 타입과 환경 변수 helper.
- `Server/Registry`: embedded registry process.
- `Server/Provider`: provider process, framework handler, evidence/admin HTTP endpoint.
- `Client`: scenario client process.

## 실행

```bash
./run_e2e.sh
```

runner는 Gradle `installDist`를 먼저 실행한 뒤 registry, provider, client role binary를 각각 띄운다.
실행 로그는 `logs/<run-id>/` 아래에 남는다.

현재 Java 구현은 `feature-map.ko.md`에 적은 scenario만 완료로 본다. public API나 harness 제어가 더
필요한 항목은 gap으로 남긴다.
