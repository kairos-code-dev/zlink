# Node.js YieldDispatch E2E feature map

기준 문서: `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`

현재 상태: Node.js `YieldDispatch` config는 YD-A1~YD-E5 범위를 구현했다. Registry, Delay,
Play, Session, Client role과 `run_e2e.sh`가 있으며, Client는 stream connector로 Session gateway에
접속한다. HTTP는 readiness와 evidence 조회에만 사용한다.

Session은 stream packet을 받아 `EnsureSpotReq`와 evidence wait request는 Play control route로 전달한다.
Spot command는 public `ZLinkSpotOutbound.sendToSpot(...)`으로 전달한다. 이 경로가 registry 기반 Spot
address discovery를 쓰기 때문에 Session도 `yield.spot` Spot mesh router를 가진다. 이 router는
application helper가 아니라 public framework topology 설정이다.

| Scenario | 상태 | 근거 |
|----------|------|------|
| YD-A1 | done | `logs/20260629-235727-1952125`에서 `scenario YD-A1 passed`. Hold handler가 delay request를 기본 submit으로 기다린 뒤 같은 Spot의 probe가 실행되는 marker order를 검증했다. |
| YD-A2 | done | `logs/20260629-235727-1952125`에서 `scenario YD-A2 passed`. `ZLinkRequestCall.yield<TReply>()` 중 같은 Spot의 probe가 먼저 실행되고 continuation이 재개되는 marker order를 검증했다. |
| YD-A3 | done | `logs/20260629-235727-1952125`에서 `scenario YD-A3 passed`. `.NET`과 같은 request id, spot rid, correlation id, yield continuation marker order를 검증했다. stream metadata 직접 노출은 Spot request handler public surface가 아니므로 이 시나리오의 완료 조건에 넣지 않는다. |
| YD-A4 | done | `logs/20260629-235727-1952125`에서 `scenario YD-A4 passed`. `runWorker(...).yield()` 중 같은 Spot의 probe가 먼저 실행되고 worker continuation이 재개되는 marker order를 검증했다. |
| YD-B1 | done | `logs/20260629-235727-1952125`에서 `scenario YD-B1 passed`. actor A가 yield로 delay reply를 기다리는 동안 actor B의 fast request가 먼저 완료되는 marker order와 서로 다른 actor mailbox id를 검증했다. Node stream connector는 같은 connector에서 동시에 두 request/reply를 열지 않으므로 client driver가 두 connector를 같은 actor set에 바인딩한다. |
| YD-B2 | done | `logs/20260629-235727-1952125`에서 `scenario YD-B2 passed`. actor A가 yield 중일 때 같은 actor A의 fast packet을 같은 connector에서 send로 전달했고, fast marker가 yield continuation과 completion 뒤에만 기록되는지 검증했다. |
| YD-B3 | done | `logs/20260629-235727-1952125`에서 `scenario YD-B3 passed`. actor A의 `joinSpot(...).yield()` 대기 중 actor B fast request가 먼저 완료되고, actor A join continuation은 target Spot join 뒤에 재개되는 marker order를 검증했다. |
| YD-C1 | done | `logs/20260629-235727-1952125`에서 `scenario YD-C1 passed`. timer A가 yield로 delay reply를 기다리는 동안 timer B fast tick이 먼저 완료되는 marker order와 timer id 분리를 검증했다. |
| YD-C2 | done | `logs/20260629-235727-1952125`에서 `scenario YD-C2 passed`. 같은 timer의 다음 tick이 yield continuation과 completion 뒤에 실행되는 marker order를 검증했다. |
| YD-C3 | done | `logs/20260630-001519-2006872`에서 `scenario YD-C3 passed`. joined Spot actor yield 중 timer fast tick이 먼저 완료되고, timer yield 중 actor fast request가 먼저 완료되는 marker order를 검증했다. actor yield와 timer command는 같은 public session relay 경로를 두 stream connector로 사용해 pending request가 timer command를 막지 않게 했다. |
| YD-D1 | done | `logs/20260630-013124-2207115`에서 `play-a`와 `delay-a` local topology의 YD-A1/YD-C3/YD-E1 marker가 통과했다. 모든 scenario 시작 packet은 stream connector에서 session gateway로 들어간다. |
| YD-D2 | done | `logs/20260630-011052-2139060`에서 `scenario YD-D2 passed`. `play-a` owner Spot handler가 `requestToSpot(...).yield()`로 `play-b` target Spot의 `YieldReq` reply를 기다리고, owner continuation marker는 `play-a`에만 남는지 검증했다. |
| YD-D3 | done | `logs/20260630-011406-2148695`에서 `scenario YD-D3 passed`. stream connector request가 session gateway를 거쳐 `play-b` target Spot으로 relay되고, target Spot의 `YieldReq` handler가 `yield()` 중 같은 target Spot의 `ProbeMsg`를 먼저 처리하는 marker order를 검증했다. |
| YD-D4 | done | `logs/20260630-012503-2187844`에서 `scenario YD-D4 passed`. stream session으로 들어온 actor request가 bound actor로 relay되고, actor handler가 `yield()`로 delay reply를 기다리는 동안 continuation이 `play-a`에서 재개된다. actor handler가 bound session으로 보낸 `ActorPushNotify`는 bound session에만 도착하고, 별도 unbound session에는 도착하지 않는지 검증했다. |
| YD-E1 | done | `logs/20260630-013124-2207115`에서 `scenario YD-E1 passed`. Spot handler가 timeout보다 늦게 reply하는 delay request를 `yield()`로 기다리다가 public framework timeout error를 기록하고, 같은 Spot의 다음 `ProbeMsg`가 정상 처리되는지 검증했다. 늦게 도착한 reply가 `timeout-yield-unexpected-resumed` marker를 남기지 않는지도 확인했다. |
| YD-E2 | done | `logs/20260630-013740-2222765`에서 `scenario YD-E2 passed`. Spot handler가 server-side `AbortController` signal을 `yield()`에 전달하고, cancellation 뒤 같은 Spot의 `ProbeMsg`가 정상 처리되는지 검증했다. 늦게 도착한 reply가 `cancel-yield-unexpected-resumed` marker를 남기지 않는지도 확인했다. |
| YD-E3 | done | `logs/20260630-015152-2270680`에서 선택 실행이 통과했고, `logs/20260630-102243-3490701`에서 full 실행도 통과했다. pending yield 중 `play-a`를 종료하면 stream request가 public closed/cancelled error로 끝나고, 같은 endpoint로 `play-a`를 다시 시작한 뒤 새 Spot probe가 정상 처리되는지 검증했다. |
| YD-E4 | done | `logs/20260630-102243-3490701`에서 `scenario YD-E4 passed`. runner가 yield 시작용 HTTP endpoint/client 사용, `Server/Play` 밖의 `.yield(...)` 사용, YD scenario 파일의 connector helper 우회, shutdown scenario의 stream connector 직접 생성 누락을 정적으로 검사한다. |
| YD-E5 | done | Node report는 공통 scenario id와 marker 이름을 사용한다. 여러 framework 언어의 Config 8 report를 한 번에 모아 비교하는 aggregation은 별도 cross-language parity gate에서 수행한다. |

## 후속 계약 판정

| Scenario | 판정 | 다음 작업 |
|----------|------|-----------|
| YD-E5 | 구현 | Node report는 공통 scenario id와 marker 이름을 사용한다. cross-language aggregation은 Node config 완료 조건이 아니라 모든 언어 report가 준비된 뒤 실행할 별도 parity gate다. |

검증:

- PASS: `timeout 420s ./run_e2e.sh full`
  - 로그: `logs/20260630-102243-3490701`
  - client marker: `yield-dispatch e2e result=passed`
  - scenario marker: `scenario YD-E4 passed`, `scenario YD-A1 passed`, `scenario YD-A2 passed`, `scenario YD-A3 passed`, `scenario YD-A4 passed`, `scenario YD-B1 passed`, `scenario YD-B2 passed`, `scenario YD-B3 passed`, `scenario YD-C1 passed`, `scenario YD-C2 passed`, `scenario YD-C3 passed`, `scenario YD-D2 passed`, `scenario YD-D3 passed`, `scenario YD-D4 passed`, `scenario YD-E1 passed`, `scenario YD-E2 passed`, `scenario YD-E3 passed`
  - Shutdown marker: `yield-dispatch shutdown wait result=passed`, `yield-dispatch shutdown recovery result=passed`
  - Session evidence: `HoldMsg`, `ProbeMsg`, 두 번의 `YieldMsg`, `WorkerYieldMsg`, `TimerStartMsg`, `TimerStopMsg`, `RemoteSpotYieldReq`, `RemoteSpotYieldMsg`, `YieldTimeoutMsg`, `YieldCancelMsg`, shutdown scenario relay가 성공 marker를 남김
  - Flow log: `ActorPushYieldReq`가 stream session에서 actor request로 dispatch된 흐름을 기록
  - Play evidence: YD-A1/YD-E3 marker order와 shutdown recovery 검증에 필요한 marker 모두 기록
