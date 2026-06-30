# Node Config 1 Registry Messaging E2E

이 디렉터리는 `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md` 기준의
Node RegistryMessaging E2E 앱이다. `.NET` 기준 구현은
`framework/languages/dotnet/e2e/RegistryMessaging` 이다.

현재 포팅은 `.NET`과 같은 역할 분리를 유지한다.

- `Server/Registry`: standalone registry와 topology 조회 endpoint
- `Server/Provider`: profile provider, manual client, route mesh provider
- `Server/Workflow`: workflow channel provider
- `Server/Consumer`: client-only profile consumer
- `Client`: scenario ID별 검증 실행

실행:

```bash
./run_e2e.sh
```
