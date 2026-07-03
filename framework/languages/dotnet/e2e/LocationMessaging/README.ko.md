# .NET Config 1 Location Messaging E2E

이 디렉토리는 `framework/doc/framework/common/e2e/config-1-location-messaging.ko.md` 기준의
`.NET` location store 기반 messaging E2E 앱이다. registry process는 없다. 각 노드가
공식 Redis location store extension(`AddRedisLocationStore`)으로 자기 peer location row를
자동 등록하고, 검증은 `IZLinkLocationRuntimeQuery.ListPeersAsync(filter)`(raw row)와
`IZLinkPeerLocationResolver.ListPeersAsync(..., Refresh)`(member peer 사용자 기능)로 나눈다.

현재 구현된 시나리오:

- `RM-A1` location store 자동 연결 + rid 자동 resolve
- `RM-A2` 수동 endpoint 연결
- `RM-A4` 같은 rid, 다른 endpoint failover
- `RM-B1` scale-out
- `RM-B2` scale-in / graceful drain
- `RM-C1` request / send happy path
- `RM-C2` targeted request by rid
- `RM-C3` 다중 provider 분산
- `RM-C4` timeout과 late reply 비오염
- `RM-C5` 미등록 packet 처리
- `RM-C7` weighted 분산
- `RM-C8` payload 크기 변주
- `RM-C9` backpressure 관측

P1/P2 시나리오는 공통 문서의 지원 조건과 미배선 사유를 그대로 따른다.

실행:

```bash
./run_e2e.sh
```

Redis는 `ZLINK_REDIS_E2E_ENDPOINT`가 설정되어 있으면 그 instance를 쓰고, 없으면
disposable `redis:7-alpine` container를 띄운다. 실행마다 run-unique key prefix
(`zlink:e2e:cfg1:<epoch>-<pid>`)로 격리하고, 종료 시 container 또는 해당 prefix의
key를 정리한다.
