# Node Config 1 Location Messaging E2E

이 디렉터리는 `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md` 기준의
Node location messaging E2E 앱이다. `.NET` 기준 구현은
`framework/languages/dotnet/e2e/RegistryMessaging` 이다.

현재 포팅은 `.NET`과 같은 역할 분리를 유지한다.

- `Server/LocationProbe`: Redis location store의 peer row를 조회하는 topology endpoint
- `Server/Provider`: profile provider, manual client, route mesh provider
- `Server/Workflow`: workflow channel provider
- `Server/Consumer`: client-only profile consumer
- `Client`: scenario ID별 검증 실행

실행:

```bash
./run_e2e.sh
```
