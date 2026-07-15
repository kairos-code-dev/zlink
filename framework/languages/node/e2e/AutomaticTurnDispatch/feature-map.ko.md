# Node.js AutomaticTurnDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-automatic-turn-dispatch.ko.md`

현재 상태: Node.js `AutomaticTurnDispatch` config는 ATD-A1~ATD-E5 범위를 구현했다. Delay,
Play, Session, Client role과 `run_e2e.sh`가 있으며, Client는 stream connector로 Session gateway에
접속한다. HTTP는 readiness와 evidence 조회에만 사용한다.

Session은 stream packet을 받아 `EnsureSpotReq`와 evidence wait request는 Play control route로 전달한다.
Spot command는 public `ZLinkSpotOutbound.sendToSpot(...)`으로 전달한다. 이 경로가 location store 기반 Spot
address resolution을 쓰기 때문에 Session도 `await.spot` Spot mesh router를 가진다. 이 router는
application helper가 아니라 public framework topology 설정이다.

| Scenario | 상태 | 근거 |
|----------|------|------|
| ATD-A1 | done | `logs/20260702-051530-20410`에서 `scenario ATD-A1 passed`. Hold handler가 delay request를 기본 submit으로 기다린 뒤 같은 Spot의 probe가 실행되는 marker order를 검증했다. |
| ATD-A2 | done | `logs/20260702-051530-20410`에서 `scenario ATD-A2 passed`. `ZLinkRequestCall.submit<TReply>()` 중 같은 Spot의 probe가 먼저 실행되고 continuation이 재개되는 marker order를 검증했다. |
| ATD-A3 | done | `logs/20260702-051530-20410`에서 `scenario ATD-A3 passed`. `.NET`과 같은 request id, spot rid, correlation id, await continuation marker order를 검증했다. stream metadata 직접 노출은 Spot request handler public surface가 아니므로 이 시나리오의 완료 조건에 넣지 않는다. |
| ATD-A4 | done | `logs/20260715-092009-2649385`에서 `scenario ATD-A4 passed`. `runIoWorker(...).yield()`로 turn을 반납한 동안 같은 Spot의 probe가 먼저 실행되고 worker continuation이 재개되는 marker order를 검증했다. |
| ATD-B1 | done | `logs/20260702-051530-20410`에서 `scenario ATD-B1 passed`. actor A가 await로 delay reply를 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker order와 서로 다른 actor mailbox id를 검증했다. Node stream connector는 같은 connector에서 동시에 두 request/reply를 열지 않으므로 client driver가 두 connector를 같은 actor set에 바인딩한다. |
| ATD-B2 | done | `logs/20260702-051530-20410`에서 `scenario ATD-B2 passed`. actor A가 await 중일 때 같은 actor A의 fast packet을 같은 connector에서 send로 전달했고, fast marker가 await continuation과 completion 뒤에만 기록되는지 검증했다. |
| ATD-B3 | done | `logs/20260702-051530-20410`에서 `scenario ATD-B3 passed`. actor A의 `joinSpot(...).submit()` 대기 중 actor B fast request가 먼저 완료되고, actor A join continuation은 target Spot join 뒤에 재개되는 marker order를 검증했다. |
| ATD-C1 | done | `logs/20260702-051530-20410`에서 `scenario ATD-C1 passed`. timer A가 await로 delay reply를 기다리는 동안 timer B fast tick이 먼저 완료되는 marker order와 timer id 분리를 검증했다. |
| ATD-C2 | done | `logs/20260702-051530-20410`에서 `scenario ATD-C2 passed`. 같은 timer의 다음 tick이 await continuation과 completion 뒤에 실행되는 marker order를 검증했다. |
| ATD-C3 | done | `logs/20260702-051530-20410`에서 `scenario ATD-C3 passed`. joined Spot actor await 중 timer fast tick이 먼저 완료되고, timer await 중 actor fast request가 먼저 완료되는 marker order를 검증했다. actor await와 timer command는 같은 public session relay 경로를 두 stream connector로 사용해 pending request가 timer command를 막지 않게 했다. |
| ATD-D1 | done | `logs/20260702-051530-20410`에서 `play-a`와 `delay-a` local topology의 ATD-A1/ATD-C3/ATD-E1 marker가 통과했다. 모든 scenario 시작 packet은 stream connector에서 session gateway로 들어간다. |
| ATD-D2 | done | `logs/20260702-051530-20410`에서 `scenario ATD-D2 passed`. `play-a` owner Spot handler가 `requestToSpot(...).submit()`로 `play-b` target Spot의 `AwaitReq` reply를 기다리고, owner continuation marker는 `play-a`에만 남는지 검증했다. |
| ATD-D3 | done | `logs/20260702-051530-20410`에서 `scenario ATD-D3 passed`. stream connector request가 session gateway를 거쳐 `play-b` target Spot으로 relay되고, target Spot의 `AwaitReq` handler가 `await()` 중 같은 target Spot의 `ProbeMsg`를 먼저 처리하는 marker order를 검증했다. |
| ATD-D4 | done | `logs/20260702-051530-20410`에서 `scenario ATD-D4 passed`. stream session으로 들어온 actor request가 bound actor로 relay되고, actor handler가 `await()`로 delay reply를 기다리는 동안 continuation이 `play-a`에서 재개된다. actor handler가 bound session으로 보낸 `ActorPushNotify`는 bound session에만 도착하고, 별도 unbound session에는 도착하지 않는지 검증했다. |
| ATD-E1 | done | `logs/20260702-051530-20410`에서 `scenario ATD-E1 passed`. Spot handler가 timeout보다 늦게 reply하는 delay request를 `await()`로 기다리다가 public framework timeout error를 기록하고, 같은 Spot의 다음 `ProbeMsg`가 정상 처리되는지 검증했다. 늦게 도착한 reply가 `timeout-await-unexpected-resumed` marker를 남기지 않는지도 확인했다. |
| ATD-E2 | done | `logs/20260702-051530-20410`에서 `scenario ATD-E2 passed`. Spot handler가 server-side `AbortController` signal을 `await()`에 전달하고, cancellation 뒤 같은 Spot의 `ProbeMsg`가 정상 처리되는지 검증했다. 늦게 도착한 reply가 `cancel-await-unexpected-resumed` marker를 남기지 않는지도 확인했다. |
| ATD-E3 | done | `logs/20260708-141507-206725`에서 `timeout 420s ./run_e2e.sh ATD-E3`가 통과했다. pending await 중 `play-a`를 종료한 뒤 shutdown wait client가 public closed/cancelled error를 관찰했고, `play-a` 재시작 뒤 recovery probe도 통과했다. runner 출력에 `await-dispatch shutdown wait result=passed`, `await-dispatch shutdown recovery result=passed`, `scenario ATD-E3 passed`, `await-dispatch e2e result=passed`가 남았다. 실행 중 `session-a`가 한동안 높은 CPU를 사용했지만 timeout이나 retry가 아니라 graceful shutdown 완료 뒤 pass했다. |
| ATD-E4 | done | `logs/20260702-051530-20410`에서 `scenario ATD-E4 passed`. runner가 await 시작용 HTTP endpoint/client 사용, `Server/Play` 밖의 `.submit(...)` 사용, YD scenario 파일의 connector helper 우회, shutdown scenario의 stream connector 직접 생성 누락을 정적으로 검사한다. |
| ATD-E5 | done | Node report는 공통 scenario id와 marker 이름을 사용한다. 여러 framework 언어의 Config 8 report를 한 번에 모아 비교하는 aggregation은 별도 cross-language parity gate에서 수행한다. |

## 후속 계약 판정

| Scenario | 판정 | 다음 작업 |
|----------|------|-----------|
| ATD-E5 | 구현 | Node report는 공통 scenario id와 marker 이름을 사용한다. cross-language aggregation은 Node config 완료 조건이 아니라 모든 언어 report가 준비된 뒤 실행할 별도 parity gate다. |

검증:

- PASS: `timeout 420s ./run_e2e.sh full`
  - 로그: `logs/20260702-065606-73391`
  - 이전 로그: `logs/20260702-051530-20410`
  - ATD-E3 선택 재검증: `logs/20260708-141507-206725`
  - Shutdown marker: `await-dispatch shutdown wait result=passed`, `await-dispatch shutdown recovery result=passed`
  - Session evidence: `HoldMsg`, `ProbeMsg`, 두 번의 `AwaitMsg`, `WorkerAwaitMsg`, `TimerStartMsg`, `TimerStopMsg`, `RemoteSpotAwaitReq`, `RemoteSpotAwaitMsg`, `AwaitTimeoutMsg`, `AwaitCancelMsg`, shutdown scenario relay가 성공 marker를 남김
  - Flow log: `ActorPushAwaitReq`가 stream session에서 actor request로 dispatch된 흐름을 기록
  - Play evidence: ATD-A1/ATD-E3 marker order와 shutdown recovery 검증에 필요한 marker 모두 기록
