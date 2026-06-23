# .NET Config 1 Registry Messaging E2E

이 디렉토리는 `framework/doc/framework/common/e2e/config-1-registry-messaging.ko.md` 기준의
`.NET` Registry messaging E2E 앱이다.

현재 구현된 시나리오:

- `RM-A1` registry 자동 연결 + rid 자동 resolve
- `RM-A2` 수동 endpoint 연결
- `RM-A4` 같은 rid, 다른 endpoint failover
- `RM-B1` scale-out
- `RM-B2` scale-in / graceful drain
- `RM-C1` request / send happy path
- `RM-C2` targeted request by rid
- `RM-C3` 다중 provider 분산
- `RM-C4` timeout과 late reply 비오염
- `RM-C5` 미등록 packet 처리
- `RM-C6` dealer mesh peer request

P1/P2 시나리오는 공통 문서의 지원 조건과 미배선 사유를 그대로 따른다.

실행:

```bash
./run_e2e.sh
```
