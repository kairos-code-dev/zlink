# Kotlin YieldDispatch E2E feature map

이 문서는 `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`를
Kotlin framework E2E에서 어떤 공개 API 경로로 검증하는지 정리한다.
현재 module runner는 `logs/20260630-114116-3823326`에서 `Client`, `Server/Registry`,
`Server/Delay`, `Server/Play`, `Server/Session` binary를 실행하고, D2 전용 mode에서 `play-b`도 추가로
실행한다. 이 로그에서 `YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`, `YD-B1`, `YD-C1`,
`YD-C2`, `YD-E1`, `YD-D1`, `YD-D2`, `YD-D3` marker와 `yield-dispatch kotlin e2e result=passed`를 확인했다.

## 현재 runner 통과 항목

아래 항목은 현재 Kotlin runner에서 marker를 확인한 범위다. `.NET` 기준 scenario 의미, evidence
surface, message contract가 아직 1:1로 맞지 않는 항목은 `porting-inventory.ko.md`에서 partial로
유지한다.

- `YD-A1`: stream connector가 session gateway를 통해 `HoldMsg`와 `ProbeMsg`를 같은 Spot에
  보내고, Spot handler가 public `requestToChannel(...).await(...)`로 기다리는 동안 같은 Spot의
  다음 command가 먼저 실행되지 않는지 evidence 순서로 확인한다.
- `YD-A2`: stream connector가 session gateway를 통해 `YieldMsg`와 `ProbeMsg`를 같은 Spot에
  보내고, Spot handler가 public `requestToChannel(...).yield(...)`로 turn을 반납한 뒤 같은 Spot의
  독립 command가 먼저 실행되고 continuation이 이후 재개되는지 evidence 순서로 확인한다.
- `YD-A3`: 한 spot이 yield 중이어도 다른 spot 요청은 독립적으로 처리되는지 확인한다.
- `YD-A4`: stream connector가 session gateway를 통해 `WorkerYieldMsg`와 `ProbeMsg`를 같은
  Spot에 보내고, public `context.runWorker(...).yield()`가 worker 대기 중 turn을 반납한 뒤 같은
  Spot의 독립 command가 먼저 실행되고 continuation이 이후 재개되는지 evidence 순서로 확인한다.
- `YD-B1`: stream session에 bind된 actor A가 public `requestToChannel(...).yield(...)`로 delay
  service를 기다리는 동안 다른 stream session에 bind된 actor B의 fast request가 actor A continuation보다
  먼저 완료되는지 evidence 순서로 확인한다.
- `YD-C1`: timer handler가 delay channel yield를 기다리는 동안 같은 spot의 다른 timer tick이
  진행되는지 node-level evidence store로 확인한다.
- `YD-C2`: 같은 timer의 다음 tick이 첫 yield 완료 뒤 처리되는지 확인한다.
- `YD-E1`: `yield(...)` timeout 뒤 같은 Spot이 probe packet을 계속 처리하는지 node-level evidence
  store로 확인한다.
- `YD-D1` 일부: 현재 runner가 `play-a`와 `delay-a` local topology에서 A/B/C/E1 marker를 통과한 뒤
  local topology aggregate marker를 남긴다.
- `YD-D2` 일부: 기본 local sweep 뒤 `play-b`를 추가로 띄우고 D2 전용 client mode에서 `play-a`
  owner Spot이 public `requestToSpot(...).yield(...)`로 `play-b` target Spot을 기다리는지 확인한다.
  owner evidence에는 `remote-yield-*`, target evidence에는 `yield-*` marker만 남는지 검증한다.
- `YD-D3` 일부: D2 전용 topology에서 stream connector packet이 Session route mesh를 거쳐 `play-b`
  target Spot으로 전달되고, target Spot의 `YieldMsg` handler가 yield 중일 때 같은 target Spot의
  `ProbeMsg` marker가 먼저 실행되는지 검증한다.
- `YD-E4` 일부: runner가 HTTP trigger/client 사용, Play/Session Entry Spot join 예외 밖의 `yield(...)`
  사용, connector를 받지 않는 `Yd*.java` scenario file, scenario file의 connector 생성과 lifecycle 소유를
  정적으로 검사한다.

## 남은 gap

- actor yield 중 timer fast tick, timer yield 중 actor fast request를 교차시키는 `YD-C3`는 현재
  Kotlin runner에서 public stream actor request가 timeout되어 완료 marker로 올리지 않는다. 실패한
  시도는 `logs/20260630-071558-3077374`, `logs/20260630-071810-3084675`에서 확인했다.
- session relay actor yield와 bound/unbound session push 격리를 검증하는 `YD-D4`는 public
  `boundSession().send(...)`와 stream connector `waitFor(...)` 조합으로 시도했지만 full runner에
  넣으면 기존 actor request sweep이 timeout되었다. `logs/20260630-074648-3163279`에서는 D4 전용
  client marker가 통과했지만 같은 topology를 이어서 쓰는 main sweep이 `YD-B2`에서 timeout되었고,
  `logs/20260630-074614-3161019`, `logs/20260630-074527-3158860`에서는 D4 전용 actor join부터
  timeout되었다. 따라서 현재는 full runner 통과 항목으로 올리지 않는다.
- `YD-E2` cancellation은 Java/Kotlin public `ZLinkRequestCall`에 `.NET`의
  `Yield<T>(CancellationToken)`처럼 yield 대기를 외부 cancellation token으로 끊는 overload가 없어
  같은 수준으로 구현하지 않는다. 현재 public surface는 `timeout(...)`, `submit(...)`,
  `yield(...)`, `await(...)`만 제공하므로 timeout 검증(`YD-E1`)과 cancellation 검증을 같은 것으로
  처리하지 않는다.
- `YD-E3` shutdown/recovery는 아직 별도 runner mode로 분리되어 있지 않다. `.NET` runner는 pending
  yield marker를 확인한 뒤 `play-a`를 종료하고, client가 public closed/cancelled 계열 오류를
  관찰했는지 확인한 다음 같은 `play-a` rid로 재시작해 recovery probe marker를 검증한다. 현재 Kotlin
  runner는 `play-a`를 한 번 시작한 뒤 종료/재시작하지 않으며, shell에서 기다릴 수 있는 Play evidence
  log나 shutdown-wait/recovery client mode도 없다. 따라서 정상 완료 경로와 serial dispatch 의미만
  고정한다.
- `.NET` 기준의 `Client`, `Shared`, `Server/Registry`, `Server/Delay`, `Server/Play`,
  `Server/Session` role project는 추가했다. 현재 구현된 client marker 중 `YD-A1`, `YD-A2`,
  `YD-A3`, `YD-A4`, `YD-B1`, `YD-C1`, `YD-C2`, `YD-E1`, `YD-D1`은
  scenario file로 분리했다.
- `YD-B2`는 같은 actor의 slow/fast request 순서와 대기 시간을 검증하려는 scenario file은 남아 있지만
  현재 runner 완료 범위에는 넣지 않는다. `.NET`의
  `ActorYieldReq`/`ActorFastReq` evidence contract와 아직 같지 않다. 같은 connector에서
  `ActorYieldReq`/`ActorFastReq`를 pending으로 둔 시도는 `logs/20260630-104321-3583163`,
  `logs/20260630-104727-3598586`, `logs/20260630-104910-3604933`에서 timeout되어 완료로 올리지
  않는다. 현재 `ProbeReq` 기반 부분 시나리오도 full runner 안에서는 `logs/20260630-113806-3810792`,
  `logs/20260630-114012-3819684`에서 route mesh timeout이 재현되어 runner 완료 marker에서 제외한다.
- `YD-B3`는 actor handler의 public `joinSpot(...).yield(...)` 대기 중 다른 actor progress를
  검증하려는 scenario file은 남아 있지만 현재 runner 완료 범위에는 넣지 않는다. `.NET`의
  `ActorJoinYieldReq`/`ActorFastReq` evidence contract와 아직 같지 않다.
  `ActorJoinYieldReq` 기반 정렬 시도는 `logs/20260630-105615-3628535`,
  `logs/20260630-110000-3641656`, `logs/20260630-110248-3653255`에서 timeout 또는 marker 순서
  불일치로 실패해 완료로 올리지 않는다. 현재 `ActorJoinReq`/`ProbeReq` 기반 부분 시나리오도
  `logs/20260630-112723-3766598`, `logs/20260630-113906-3813484`에서 timeout되어 runner 완료
  marker에서 제외한다.
- `.NET` feature-map에서 구현으로 표시한 `YD-C3`,
  `YD-D4`, `YD-E2`, `YD-E3`은 아직 Kotlin에서 같은 수준으로
  검증하지 않는다. `YD-E4`는 정적 검사 일부만 추가했으며, scenario file이 connector 생성과 lifecycle을
  소유하지 못하게 막는다. 다만 `.NET`처럼 모든 client scenario가 thin helper 없이 connector를 직접 쓰는
  구조까지는 아직 맞추지 않았다.
- `.NET`도 `YD-A3` metadata 보존과 `YD-E5` cross-language report aggregation은 부분 구현으로
  남기므로 Kotlin에서도 완료로 표시하지 않는다.
