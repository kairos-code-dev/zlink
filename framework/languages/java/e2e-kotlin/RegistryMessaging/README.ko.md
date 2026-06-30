# Kotlin Config 1 Registry Messaging E2E

이 디렉토리는 `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md` 기준의 Kotlin
Registry messaging E2E 앱이다.

구조는 `.NET` 기준과 같은 의미로 role을 나눈다.

- `Shared`: client와 server가 함께 쓰는 message DTO.
- `Server/Registry`: registry server와 topology query endpoint.
- `Server/Provider`: profile channel provider, manual client, route mesh provider endpoint.
- `Server/Consumer`: direct, single, discovery consumer endpoint.
- `Server/Workflow`: workflow channel provider endpoint.
- `Client`: role server HTTP endpoint를 호출하는 scenario runner.

구현된 scenario는 `feature-map.ko.md`에 기록한다. `RM-C9`는 one-way send가 public 완료 객체나
bounded-failure oracle을 노출하지 않는다는 공통 계약에 맞춰 send pressure와 recovery를 검증한다.

실행:

```bash
./run_e2e.sh
./run_e2e.sh RM-A1
```
