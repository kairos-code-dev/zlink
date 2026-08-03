# 2026-08-04 runtime 수정, unit test, sample process와 common internals 대조

## 현재 판정

이번 checkpoint는 session route seal에서 application frame이 거절되던 runtime gap을
수정하고, 그 변경을 owner layer unit test와 실제 `.NET` sample process로 확인한 결과를
기록한다. `ZW-B1`과 일반 ZoneWorld client batch는 수정 후 통과했고, 독립 sample runner의
TicTacToe·Bingo·SupportChat·ShoppingMall·DeliveryDispatch·GameQuest도 exit `0`으로
완료했다.

다만 ZoneWorld 전체 runner는 아직 완료가 아니다. 최신 전체 실행에서는 `ZW-B4`와 `ZW-C2`까지
통과한 뒤 `ZW-C2`가 재시작한 `zone-node-2`의 bootstrap이 `bot-se-y` remote Actor create
응답을 받지 못하고 `TaskCanceledException`으로 중단됐다. 따라서 정상 sample process 증거와
전체 disruption/replacement gate를 분리한다.

## 1. route seal 실패와 원인

common internals의
[`05-relocation-continuity.ko.md`](../../../framework/common/internals/05-relocation-continuity.ko.md)는
relocation 중 route seal을 application rejection으로 처리하지 않고, 현재 stream 작업이
새 owner route가 게시될 때까지 frame을 유지한 뒤 admission을 다시 확인하도록 요구한다.

이전 구현은 `ZLinkSessionActorCoordinator.RelayToActorAsync`에서
`TryAcceptSessionActorFrame`이 `false`를 반환하면 즉시 `Unavailable`을 발생시켰다. 실제
`ZW-B1`에서는 target actor의 `route_commit_accepted` 뒤 gateway binding이 아직 sealed인
순간 다음 `MoveMsg`가 `session_frame_refused reason=route_sealed`로 버려졌고, 뒤늦게
`route_unseal_result ack=True`가 기록되어 client가 `ZoneStateNotify`를 기다리다 timeout됐다.
이 경로는 frame이 유실되었으므로 sample retry로 감추지 않고 session binding owner에서
수정해야 했다.

## 2. 수정 내용

다음 owner-layer 경로를 추가했다.

- [`ZLinkSessionActorBindingTable.cs`](../../../../languages/dotnet/src/Zlink.Framework/Runtime/Actors/ZLinkSessionActorBindingTable.cs)는
  sealed binding마다 `RouteAvailableSignal`을 두고, replacement commit/unseal,
  abort, unbind, tombstone, runtime reset에서 대기 중인 frame을 깨운다.
- [`ZLinkActorBoundSessionCoordinator.cs`](../../../../languages/dotnet/src/Zlink.Framework/Runtime/Host/ZLinkActorBoundSessionCoordinator.cs)와
  [`ZLinkFrameworkRuntimeActors.cs`](../../../../languages/dotnet/src/Zlink.Framework/Runtime/Host/ZLinkFrameworkRuntimeActors.cs)는
  이 signal을 runtime 내부 operation으로 전달한다.
- [`ZLinkSessionActorCoordinator.cs`](../../../../languages/dotnet/src/Zlink.Framework/Runtime/Streams/ZLinkSessionActorCoordinator.cs)는
  현재 stream dispatch가 보유한 payload를 폐기하지 않고 route availability를
  비동기 대기한 뒤 `TryAcceptSessionActorFrame`을 다시 호출한다. binding 자체가 없어지면
  그때만 typed `Unavailable`로 종료한다.

이 수정은 application code에 retry, sleep, raw frame 해석을 추가하지 않는다. stream session
runtime은 한 dispatch를 완료한 뒤 다음 frame을 읽으므로 current frame 보유량은 기존 stream
serial dispatch와 inbound HWM 범위 안에 있다. 다만 common internals가 정의한 relocation
hold의 일반 상한 `1024 messages / 16 MiB`를 별도 relocation mailbox counter로 구현한 것은
아니므로, 이 checkpoint에서는 route-seal 유실을 닫았다고 기록하고 일반 relocation hold
accounting parity는 열린 항목으로 남긴다.

## 3. unit test evidence

현재 source에서 다음 검증을 실행했다.

| 검증 | 결과 |
|---|---:|
| `SessionActorCoordinatorTests.Sealed_Route_Holds_Frame_Admission_Until_Unseal` 및 completed fence test | `2/2` |
| 전체 `SessionActorCoordinatorTests` | `38/38` |
| `SerialExecutorTests`, `WeightContractTests`, `ZLinkObservationQueueTests`, `RequestFailureMappingTests`, `ChannelsTests`, `SessionActorCoordinatorTests` | `122/122` |
| `EntrySpotActorDispatchTests`, `StatefulServiceRuntimeTests` | `167/167` |
| [`Zlink.Framework.SampleRegressionTests`](../../../../languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj) | `145/145` |
| HWM·serial executor·Entry Spot rekey focused filter | `35/35` |

전체 `Zlink.Framework.UnitTests`를 현재 변경으로 다시 green이라고 표시하지 않는다. 이전
광범위한 filter 실행은 200번째 이후 native testhost가
`core/src/runtime/utils/fast_mutex.hpp:61`의 `Invalid argument`로 중단된 evidence가
있으므로, 위 focused 분모와 broad suite 결과를 분리한다.

## 4. 실제 sample process evidence

### 독립 sample runner

다음 명령이 source build와 실제 process self-check를 모두 수행하고 exit `0`으로 끝났다.

```text
bash framework/languages/dotnet/samples/run_samples.sh \
  TicTacToe Bingo SupportChat ShoppingMall DeliveryDispatch GameQuest
```

runner가 출력하거나 log에서 확인한 evidence marker는 다음과 같다.

```text
bingo-placement=completed
supportchat-server-evidence=completed
shoppingmall-server-evidence=completed
deliverydispatch-runner-evidence=completed
gamequest-server-evidence=completed
```

TicTacToe는 별도 단일 marker 대신 client의 `stream-inbound`,
`observer-win-milestone=verified`와 두 Play server의 leave/destroy completion log를
runner가 확인한 뒤 exit `0`으로 종료한다.

### ZoneWorld

route-seal 수정 후 선택 실행은 다음과 같이 통과했다.

```text
bash framework/languages/dotnet/samples/ZoneWorld/run_sample.sh \
  'ZW-A1,ZW-A2,ZW-A3,ZW-A4,ZW-A5,ZW-B1'
zoneworld-batch=passed scenarios=ZW-A1,ZW-A2,ZW-A3,ZW-A4,ZW-A5,ZW-B1
```

최신 전체 실행에서는 G4/G1/G2/G5, 일반 client batch
`ZW-A1,A2,A3,A4,A5,B1,B2,B3,B5,C1,C4,D1,E1,E2,E3,E4,E6,F1,F3,F4`,
`ZW-B4`, `ZW-C2`가 통과했다. `ZW-C2` 뒤 replacement process는
`ZLinkActorManagerService.SubmitAsync`의 remote `ActorCreate`가 peer admission 수렴 중
응답을 받지 못한 채 deadline cancellation을 받아 `topology=ready`를 기록하지 못했다.
보존 evidence는 다음 디렉터리에 있다.

```text
/tmp/zlink-dotnet-zoneworld-evidence.5Oma1w/ZoneWorld/
```

따라서 ZoneWorld 전체 runner, replacement 이후의 후속 `C3`/`E5`/browser 및 최종
aggregate gate는 미완료다. standalone `ZW-B4`와 latest full-run `ZW-B4`가 통과한 사실은
이전 B4 timeout을 수정 완료로 단정하는 근거가 아니라, 전체 실행 순서와 replacement
startup을 별도 재현해야 한다는 evidence다.

## 5. common internals 대조

대조 대상은 `framework/doc/framework/common/internals/README.ko.md`와
`01-layering`부터 `11-message-ownership`, `service-wire-protocol`까지다.

현재 변경에서 확인된 항목은 다음과 같다.

- `02-serialization`: actor payload와 SpotWide admission의 queue 경계를 유지하고,
  application/lifecycle lane을 count와 bytes로 각각 제한했다. lifecycle priority에는
  continuous limit과 yield debt를 적용했으며, normal lane 앞 삽입은 추가하지 않았다.
- `03-progress-isolation`: application callback이 lifecycle turn에서 framework call을
  기다릴 때 application lane으로 yield한다. `ZLinkSpotSerialExecutor`의
  `ExecuteApplicationCallbackAsync`와 lifecycle callback regression이 이를 확인한다.
- `04-completion`: completion table의 one-winner, early response, tombstone와 bounded
  pending mapping을 유지했다. accepted operation을 caller retry로 반복하지 않는다.
- `05-relocation-continuity`: 이번 route-available signal로 seal을 frame rejection이
  아닌 bounded stream admission wait로 바꿨다. commit 뒤 source rollback을 하지 않는
  fence와 existing relocation generation checks도 유지했다.
- `06-routing-and-cache`: smooth weighted selector와 deterministic RID tiebreak, positive
  route selection 및 receive batch의 count/bytes/time budget을 적용했다. direct target은
  다른 후보로 바꾸지 않는다.
- `07-dispatch-loop`: ready owner를 확인한 뒤 atomic claim/enqueue를 사용하고,
  application receive batch는 `ZLinkReceiveBatchBudget`의 count·bytes·time 제한을 따른다.
  callback은 payload ownership을 queue로 넘긴 뒤 반환한다.
- `08-object-lifecycle`: logical Entry Spot rekey 뒤 같은 managed entry object를 반환하고,
  actor owner location을 logical Entry Spot에 유지한다. idle eviction은 active work와
  queue count/bytes를 확인한 뒤 lifecycle order로 처리한다.
- `09-session-binding`: session binding gate와 actor execution gate를 분리하고, route seal
  signal을 runtime control 경로에서 처리한다. seal 중 application frame을 application
  queue에 새로 복제하지 않는다.
- `10-liveness-and-state`: observation queue는 latest 상태와 terminal/loss counter를
  bounded하게 유지하며 callback을 lock 밖에서 실행한다.
- `11-message-ownership`: queue가 admitted payload를 소유하고 typed handler 진입 뒤
  decode한다. route hold 수정은 caller raw bytes 우회나 sample codec 추가를 사용하지 않는다.

다음 항목은 internals와 아직 완전히 일치하지 않으므로 완료로 표시하지 않는다.

1. `service-wire-protocol`에 relay-success notification command/schema가 없다. W3는 common
   wire contract를 먼저 설계해야 하며, 현재 Framework 내부 notification이나 sample polling으로
   우회하지 않았다.
2. `ZLinkManagedMeshNode.SnapshotChannelTargets`와 ClientServer `SelectReady`는 selection
   호출마다 candidate snapshot/filter/sort 배열을 만든다. 기능 결과는 맞지만 internals의
   steady-state allocation 목표와 다르다.
3. timer마다 `Task.Delay`와 개별 timer lane을 사용하는 경로가 남아 있어 `07-dispatch-loop`의
   shared scheduler 목표와 구조적으로 다르다.
4. .NET Spot 구현은 semantic kind를 enum과 하나의 activation에 담고 있어
   `08-object-lifecycle`의 개념적 Spot kind 분리와 완전히 같지 않다.
5. observation stop 경로 중 `observer.Complete()`로 stream을 닫는 경로가 있어,
   application cancellation 외에는 observation stream을 닫지 않는 `10-liveness-and-state`
   규칙과 재검토가 필요하다.
6. broad current UnitTests, current-source package-only process, Config 14 Instance Spot,
   independent final audit은 아직 별도 gate다.

이 log는 route-seal runtime gap과 그 owner-layer regression, 독립 sample process 결과를
기록하지만 Phase A/Phase B 전체 완료 판정을 내리지 않는다.
