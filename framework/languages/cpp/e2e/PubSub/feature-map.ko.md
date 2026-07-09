# C++ Pub/Sub E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-3-pubsub.ko.md`

이 문서는 Config 3 Pub/Sub 공통 시나리오 중 C++ framework E2E가 현재 검증하는 항목과,
public API 또는 harness 제어가 더 필요한 항목을 구분한다.

최신 `all` runner proof는 `logs/20260708-123833-1298240`이다. 이 실행은 Redis location store를
공유하는 Publisher/Subscriber 역할로 PS-A1~PS-C1 전체와 publisher operational endpoint snapshot을
통과했다.

| 시나리오 | 상태 | 근거 |
|----------|------|------|
| `PS-A1` | 구현 | Publisher role server가 fanout 측정 sequence를 발행하고, 세 subscriber role server의 bounded `/evidence/wait`가 공통 sequence 수신 line을 확인한다. 로그: `logs/20260708-123833-1298240`, 출력: `scenario PS-A1 passed`, `verify basic passed`, `pubsub e2e result=passed`. |
| `PS-A2` | 구현 | subscriber handler가 publish context의 topic을 보고 관심 topic은 accepted evidence로, 비관심 topic은 ignored evidence로 기록하는지 각 subscriber role server의 `/evidence/wait`로 확인한다. 로그: `logs/20260708-123833-1298240`, 출력: `scenario PS-A2 passed`, `verify topic passed`, `pubsub e2e result=passed`. |
| `PS-A3` | 구현 | late subscriber가 합류 이후 발행분만 받고 합류 전 발행분은 replay되지 않는지 subscriber role server의 bounded evidence wait와 negative line check로 확인한다. 로그: `logs/20260708-123833-1298240`, 출력: `scenario PS-A3 passed`, `verify late passed`, `pubsub e2e result=passed`. |
| `PS-A4` | 구현 | Client support가 reconnect subscriber 프로세스를 종료했다가 같은 endpoint로 다시 띄우고, 종료 중 발행분은 replay되지 않으며 재구독 이후 발행분만 다시 받는지 확인한다. 로그: `logs/20260708-123833-1298240`, 출력: `scenario PS-A4 passed`, `verify reconnect passed`, `pubsub e2e result=passed`. |
| `PS-B1` | 구현 | 한 subscriber handler에 지연을 주입한 상태에서 다른 subscriber가 같은 발행 sequence를 계속 수신하는지 빠른 subscriber의 `/evidence/wait`로 확인한다. 로그: `logs/20260708-123833-1298240`, 출력: `scenario PS-B1 passed`, `verify slow passed`, `pubsub e2e result=passed`. |
| `PS-B2` | 구현 | Client support가 Publisher role server를 종료했다가 다시 시작한 뒤, 재시작 이후 발행분을 기존 subscriber가 받는지 bounded evidence wait로 확인한다. 로그: `logs/20260708-123833-1298240`, 출력: `scenario PS-B2 passed`, `verify publisher-restart passed`, `pubsub e2e result=passed`. |
| `PS-C1` | 구현 | handler 없는 message name으로 publish하면 subscriber dispatch observer에 `handlerMissing`/`drop` marker가 남고 후속 정상 publish가 오염되지 않는지 subscriber role server의 `/evidence/wait`로 확인한다. 로그: `logs/20260708-123833-1298240`, 출력: `scenario PS-C1 passed`, `verify negative passed`, `pubsub e2e result=passed`. |

## 검증 경로 판정

Pub/Sub fanout의 수신자는 client stream session이 아니라 subscriber 역할 server다. 공통 E2E README는
이 경우 subscriber handler가 남긴 bounded `/evidence/wait` marker를 성공 기준으로 사용할 수 있다고
정리한다. C++ PubSub도 실제 subscriber role server의 `/evidence/wait`로 accepted, ignored, dispatch
error line을 기다리므로 별도 client stream connector observer gap은 남기지 않는다. 최신 runner는
검증 stdout을 `verify.log`에 저장하고, publisher role의 `/health`, `/evidence`,
`/evidence/clear`, `/shutdown` endpoint 동작도 operational log와 final evidence snapshot으로 남긴다.
