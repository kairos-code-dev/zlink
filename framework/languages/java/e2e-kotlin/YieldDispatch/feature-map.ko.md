# Kotlin YieldDispatch E2E feature map

이 문서는 `framework/doc/framework/common/e2e/config-8-yield-dispatch.ko.md`를
Kotlin framework E2E에서 어떤 공개 API 경로로 검증하는지 정리한다.
현재 module runner는 `logs/20260704-041428-16476`에서 `Client`, `Server/Delay`,
`Server/Play`, `Server/Session` binary를 실행하고, D2 전용 mode에서 `play-b`도 추가로
실행한다. registry role은 실행하지 않고 Delay/Play/Session role이 Redis location store를 공유한다.
이 로그에서 `YD-A1`, `YD-A2`, `YD-A4`, `YD-B1`, `YD-B2`, `YD-B3`, `YD-C1`, `YD-C2`,
`YD-E1`, `YD-E2`, `YD-D1`, `YD-D2`, `YD-D3` marker와 `yield-dispatch kotlin e2e result=passed`를
확인했다. `YD-A3`는 공통 문서 의미에 맞춰 다시 정렬했고,
`logs/20260707-222104-3660094/client.stdout.log`에서 단독 marker를 확인했다. `YD-B2`도
`.NET`과 같은 `ActorYieldReq`/`ActorFastReq`와 actor evidence 순서 검증으로 다시 맞췄고,
`logs/20260707-222754-3681782/client.stdout.log`에서 단독 marker를 확인했다. `YD-B3`도
`.NET`과 같은 `ActorJoinYieldReq`/`ActorFastReq`와 actor evidence 순서 검증으로 다시 맞췄고,
`logs/20260707-224427-3751196/client.stdout.log`에서 단독 marker를 확인했다. `YD-C1`과
`YD-C2`도 `.NET`과 같은 unique timer Spot 생성, timer command, Play evidence 순서 검증으로 다시
맞췄고, 각각 `logs/20260707-225303-3789894/client.stdout.log`와
`logs/20260707-225337-3792863/client.stdout.log`에서 단독 marker를 확인했다. `YD-E1`도
`.NET`과 같은 `YieldTimeoutReq` reply contract, unique timeout Spot, post-timeout probe evidence
검증으로 다시 맞췄고, `logs/20260707-230235-3830539/client.stdout.log`에서 단독 marker를 확인했다.
`YD-D2`와 `YD-D3`도 remote/route-bridge topology 검증을 다시 확인했고, 각각
`logs/20260707-231120-3878253/client-d2.stdout.log`와
`logs/20260707-231217-3884460/client-d2.stdout.log`에서 단독 marker를 확인했다. `YD-D1`은
focused mode에서 A/B/C/E1 local topology scenario를 실제로 실행한 뒤 aggregate marker를 남기도록
맞췄고, `logs/20260707-233650-3991306/client.stdout.log`에서 `YD-A1`, `YD-A2`, `YD-A3`,
`YD-A4`, `YD-B1`, `YD-B2`, `YD-B3`, `YD-C1`, `YD-C2`, `YD-C3`, `YD-D4`, `YD-E1`, `YD-D1`
marker와 최종 pass marker를 확인했다. `YD-C3`는 현재 checkout에서 새 actor/timer 교차 scenario로
다시 구현했고, `logs/20260707-235604-4071087/client.stdout.log`에서 focused marker를 확인했다.
`YD-D4`도 public `boundSession().send(...)`와 stream connector `waitFor(...)` 조합으로 맞췄고,
`logs/20260708-000440-4120944/client.stdout.log`와 위 D1 aggregate runner에서 marker를 확인했다.

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
- `YD-A3`: `YieldMsg`가 같은 request id와 target spot rid로 yield 전후 evidence를 남기고,
  continuation marker가 같은 correlation id를 유지하는지 확인한다.
- `YD-A4`: stream connector가 session gateway를 통해 `WorkerYieldMsg`와 `ProbeMsg`를 같은
  Spot에 보내고, public `context.runWorker(...).yield()`가 worker 대기 중 turn을 반납한 뒤 같은
  Spot의 독립 command가 먼저 실행되고 continuation이 이후 재개되는지 evidence 순서로 확인한다.
- `YD-B1`: stream session에 bind된 actor A가 public `requestToChannel(...).yield(...)`로 delay
  service를 기다리는 동안 다른 stream session에 bind된 actor B의 fast request가 actor A continuation보다
  먼저 완료되는지 evidence 순서로 확인한다.
- `YD-B2`: 같은 actor의 slow request가 public yield로 대기하는 동안 같은 actor의 fast request가
  끼어들지 못하고, 첫 요청이 완료된 뒤 다음 요청이 처리되는지 evidence 순서로 확인한다.
- `YD-B3`: actor handler가 public `joinSpot(...).yield(...)`로 다른 Spot join을 기다리는 동안
  다른 actor request가 먼저 진행되는지 evidence 순서로 확인한다.
- `YD-C1`: unique timer Spot에서 timer A handler가 delay channel yield를 기다리는 동안 timer B의
  fast tick이 먼저 진행되고, timer A continuation이 이후 완료되는지 Play evidence 순서로 확인한다.
- `YD-C2`: unique timer Spot에서 같은 timer의 다음 tick이 첫 yield continuation과 완료 뒤 처리되는지,
  tick id와 timer mailbox evidence로 확인한다.
- `YD-C3`: unique Spot에 actor A/B를 bind한 뒤 actor A yield 중 같은 Spot의 fast timer tick이 먼저
  완료되는지 확인하고, 이어서 timer yield 중 actor B fast request가 먼저 완료되는지 같은 evidence로
  확인한다.
- `YD-E1`: unique timeout Spot에서 `yield(...)` timeout을 public reply로 관찰하고, 같은 Spot이
  post-timeout probe packet을 계속 처리하는지 Play evidence 순서로 확인한다.
- `YD-E2`: `yield(..., CancellationToken)` cancellation 뒤 같은 Spot이 probe packet을 계속 처리하는지
  node-level evidence store로 확인한다.
- `YD-D1`: focused mode가 `play-a`와 `delay-a` local topology에서 A/B/C/E1 scenario를 실제로
  실행하고 각 scenario evidence assertion이 통과한 뒤 local topology aggregate marker를 남기는지
  확인한다. `logs/20260707-233650-3991306/client.stdout.log`에서 aggregate marker와 최종 pass marker를
  확인했다.
- `YD-D2`: focused remote topology에서 `play-a` owner Spot이 public
  `requestToSpot(...).yield(...)`로 `play-b` target Spot을 기다리는지 확인한다. owner evidence에는
  `remote-yield-*`, target evidence에는 `yield-*` marker만 남고, continuation이 `play-a`로 돌아오는지
  검증한다.
- `YD-D3`: focused route-bridge topology에서 stream connector packet이 Session route mesh를 거쳐
  `play-b` target Spot으로 전달되고, target Spot의 `YieldMsg` handler가 yield 중일 때 같은 target
  Spot의 `ProbeMsg` marker가 먼저 실행되는지 검증한다.
- `YD-D4`: actor handler가 public `boundSession().send(...)`로 bound connector에만 push를 보내고,
  client가 stream connector `waitFor(...)`와 unbound connector receive count로 오배달이 없음을 확인한다.
- `YD-E4` 일부: runner가 HTTP trigger/client 사용, Play/Session Entry Spot join 예외 밖의 `yield(...)`
  사용, connector를 받지 않는 `Yd*.java` scenario file, scenario file의 connector 생성과 lifecycle 소유를
  정적으로 검사한다.

## 남은 gap

- `YD-E3` shutdown/recovery는 `YD-E3` focused repro mode까지 작성했지만 완료로 올리지 않는다. runner가
  pending yield marker를 확인한 뒤 자신이 시작한 `play-a` PID만 종료하고 같은 routing id로 재시작하면,
  기존 stream session의 recovery `ProbeReq`가 session에는 도착하지만 Play handler까지 전달되지 않고
  timeout된다. 재현 로그는 `logs/20260707-192818-3296525`이고, 버그 리포트는
  `framework/doc/plan/framework-kotlin-yd-e3-route-recovery-bug.ko.md`에 분리했다.
- `.NET` 기준의 `Client`, `Shared`, `Server/Delay`, `Server/Play`, `Server/Session`
  role project는 추가했다. registry role은 Redis location store 전환 뒤 제거했다. 현재 구현된
  client marker 중 `YD-A1`, `YD-A2`, `YD-A3`, `YD-A4`, `YD-B1`, `YD-B2`, `YD-B3`,
  `YD-C1`, `YD-C2`, `YD-C3`, `YD-D1`, `YD-D2`, `YD-D3`, `YD-D4`, `YD-E1`, `YD-E2`는 scenario file로
  분리했다.
- `.NET` feature-map에서 구현으로 표시한 항목 중 `YD-E3`은 아직 Kotlin에서 같은 수준으로
  검증하지 않는다. `YD-E4`는 정적 검사 일부만 추가했으며, scenario file이 connector 생성과 lifecycle을
  소유하지 못하게 막는다. 다만 `.NET`처럼 모든 client scenario가 thin helper 없이 connector를 직접 쓰는
  구조까지는 아직 맞추지 않았다.
- `.NET`도 `YD-E5` cross-language report aggregation은 부분 구현으로 남기므로 Kotlin에서도 완료로
  표시하지 않는다.
