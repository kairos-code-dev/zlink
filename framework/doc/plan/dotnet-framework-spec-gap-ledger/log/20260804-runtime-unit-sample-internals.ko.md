# 2026-08-04 runtime 수정, unit test, sample process와 common internals 대조

## 현재 판정

이번 checkpoint는 session route seal에서 application frame이 거절되던 runtime gap과,
같은 Core endpoint를 새 RID가 재사용할 때 이전 auto-connect target이 다음 tick까지
endpoint를 점유하던 runtime gap을 owner layer에서 수정한 결과를 기록한다. route seal은
frame admission wait로, endpoint replacement는 같은 reconcile tick 안의 disconnect 후
replacement connect로 처리한다.

현재 source 기준 `AutoConnectReconcilerTests`는 `38/38`, 관련 runtime focused 묶음은
`157/157`, Redis location repository focused 묶음은 `2/2`로 통과했다. 최신 전체 ZoneWorld
sample process도 exit `0`으로 종료했으며, 구현된 scenario와 후속 replacement·observation·
maintenance process evidence를 확인했다. 다만 `ZW-B6`는 이전 owner route를 주입하는
지원된 operational harness가 없어 runner가 의도적으로 withheld하므로, ZoneWorld aggregate를
전체 완료로 표시하지 않는다.

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
| `AutoConnectReconcilerTests` | `38/38` |
| `AutoConnectReconcilerTests`, `AutoConnectLoopTests`, `ServiceRuntimeFoundationTests`, `ActorHandoffTests` focused 묶음 | `157/157` |
| Redis provider repository authority·opaque snapshot focused 묶음 | `2/2` |
| `SerialExecutorTests`, `WeightContractTests`, `ZLinkObservationQueueTests`, `RequestFailureMappingTests`, `ChannelsTests`, `SessionActorCoordinatorTests` | `122/122` |
| `EntrySpotActorDispatchTests`, `StatefulServiceRuntimeTests` | `167/167` |
| [`Zlink.Framework.SampleRegressionTests`](../../../../languages/dotnet/tests/Zlink.Framework.SampleRegressionTests/Zlink.Framework.SampleRegressionTests.csproj) | `145/145` |
| HWM·serial executor·Entry Spot rekey focused filter | `35/35` |

전체 `Zlink.Framework.UnitTests`를 현재 변경으로 다시 green이라고 표시하지 않는다. 최신
broad 실행은 `135`개 통과 뒤 `filter.request` handler rejection으로 active test run이
aborted 되었고, 이전 broad 실행에서도 native testhost가
`core/src/runtime/utils/fast_mutex.hpp:61`의 `Invalid argument`로 중단된 evidence가 있다.
따라서 위 focused 분모와 broad suite 결과를 분리한다.

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

최신 전체 실행은 다음 명령으로 수행했고 exit `0`으로 종료했다.

```text
ZLINK_SAMPLE_EVIDENCE_DIR=/tmp/zoneworld-full-run.UQKcSm \
  bash framework/languages/dotnet/samples/ZoneWorld/run_sample.sh
```

보존 evidence는 `/tmp/zoneworld-full-run.UQKcSm/ZoneWorld`(작업 machine의 로컬 경로, 저장소 밖)이며,
`runner.log`에서 G4/G1/G2-rid/G2/G5, 일반 client batch의
`ZW-A1,A2,A3,A4,A5,B1,B2,B3,B5,C1,C4,D1,E1,E2,E3,E4,E6,F1,F3,F4`,
`ZW-B4`, `ZW-C2`, `ZW-C3`, `ZW-E5-arm`, `ZW-E5`, D1 subscriber·spot,
F1 population, F3 no-push, D2를 모두 확인했다. 최종 운영 phase marker인
`zoneworld-border-sync=completed`, `zoneworld-ops-observe=completed`,
`zoneworld-ops-announce=completed`, `zoneworld-ops-maintenance=completed`도 출력됐다.

`zoneworld-relocation=completed withheld: ZW-B6 did not pass`와
`zoneworld=completed withheld: ZW-B6 did not pass`는 실행 실패가 아니라 현재 공개
scenario surface에 B6가 없기 때문에 발생하는 의도된 판정이다. `run_sample.sh`는
이유를 이전 owner route를 주입할 지원된 operational harness 부재로 명시하고 있으며,
`Client/Scenarios.cs`에도 B6 handler가 없다. Global Actor API는 current owner를
resolve하므로 bounded Message Follow를 검증할 수 없다. 따라서 sample에 raw route 주입,
내부 helper, retry를 추가하지 않고, 이 항목은 public contract와 operational harness 설계
결정이 필요한 별도 gap으로 남긴다.

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
3. logical Spot timer는 `ZLinkTimerScheduler`의 하나의 deadline pump을 사용한다. Spot-node
   idle maintenance에는 node catalog별 `PeriodicTimer`가 남아 있어 해당 maintenance 경로의
   process·churn 측정은 별도 조건이다.
4. .NET Spot 구현은 semantic kind를 enum과 하나의 activation에 담고 있어
   `08-object-lifecycle`의 개념적 Spot kind 분리와 완전히 같지 않다.
5. observation stop 경로 중 `observer.Complete()`로 stream을 닫는 경로가 있어,
   application cancellation 외에는 observation stream을 닫지 않는 `10-liveness-and-state`
   규칙과 재검토가 필요하다.
6. owner lease가 target set에 포함된 AutoConnect loop는 repository가 global live-owner
   membership stamp를 제공하지 않으므로 row change stamp만으로 skip하면 안 된다. loop는
   lease tracker가 있는 경우 매 polling tick에서 full reconcile을 수행하고, lease가 없는
   경우에만 row stamp skip을 사용하도록 owner layer를 수정했다. 이에 따라 만료·재등장 owner를
   row write 없이도 다음 polling tick에서 다시 판정한다.
7. broad current UnitTests, current-source package-only process, Config 14 Instance Spot,
   independent final audit은 아직 별도 gate다.

이 log는 route-seal runtime gap과 그 owner-layer regression, 독립 sample process 결과를
기록하지만 Phase A/Phase B 전체 완료 판정을 내리지 않는다.

## 6. W1 HWM bounded reservation 재검증

`common/spec/06-framework-api`와 `common/internals/03-progress-isolation`에 .NET binding이
`Recv` 전에 complete message 길이를 제공하지 않는 multiplexed receive path의 정책을 명시했다.
`MaxMessageSize = M`, 동시 raw receive reservation `R`에 대해 application pending byte 상한은
`HWM + R * M`이며, control은 application pending에 넣지 않고 reservation을 반환하고,
application은 terminal 상태까지 lease를 유지한다. STREAM에서 HWM마다 raw `RecvPart`를 먼저
막던 precheck를 제거했고, session queue가 가득 차면 `ZLinkStreamInboundFrame`이 dispatch lease를
보유한 채 재시도한다. RouteMesh도 raw receive permit을 stateful/application mailbox까지 전달한다.

다음 targeted 명령은 source build를 포함해 exit `0`, `70/70`이었다.

```text
dotnet test framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Zlink.Framework.UnitTests.csproj \
  --no-restore \
  --filter 'FullyQualifiedName~SerialExecutorTests|FullyQualifiedName~InboundDispatchBudgetTests|FullyQualifiedName~StreamSession'
Passed: 70, Failed: 0, Skipped: 0, Total: 70
```

이 결과는 W1의 HWM·serial queue targeted gate만 닫는다. Mesh의 mixed control/application
process evidence, broad current UnitTests, package/clean consumer와 전체 `common/internals`
requirement-by-requirement audit은 아직 남아 있으므로 W1과 전체 구현 계획을 완료로 표시하지 않는다.

## 7. 2026-08-04 최신 gate와 internals 재검토

앞선 절의 broad test 중단 기록과 observation·message-follow 미구현 서술은 이번 절의 현재
source 결과로 대체한다. 과거 실행이 실패했다는 사실은 이력으로 보존하지만, 현재 판정에는
사용하지 않는다.

### 현재 source와 package 검증

| 검증 | 결과 |
|---|---:|
| UnitTests build (`--no-restore --no-incremental --maxcpucount:1`) | 성공, warning 0 / error 0 |
| 전체 `Zlink.Framework.UnitTests` | `1499/1499` 통과, failed 0 / skipped 0 |
| `Zlink.Framework.ContractTests` | `76/76` 통과 |
| `Zlink.Framework.SampleRegressionTests` | `145/145` 통과 |
| `verify_packaged_contract.sh` | exit 0, 9개 package manifest·hash·clean consumer·standalone HTTP consumer 통과 |
| public API snapshot | `81942c6b3c47374bab5979a4c655592956a9a2d2de0b1333b828f20e1e0b656b` |

### 실제 sample process

다음 aggregate runner가 exit `0`으로 끝났다.

```text
bash framework/languages/dotnet/samples/run_samples.sh \
  TicTacToe Bingo SupportChat ShoppingMall DeliveryDispatch GameQuest
```

TicTacToe는 두 Play server의 `LeaveGameMsg`와 Entry Spot destroy completion을 확인했고,
나머지 sample은 `bingo-placement`, `supportchat-server-evidence`,
`shoppingmall-server-evidence`, `deliverydispatch-runner-evidence`, `gamequest-server-evidence`
marker를 확인했다. 이 과정에서 sample application에 delay, retry, raw frame 처리나 새
codec을 추가하지 않았다.

### `common/internals` 현재 대조

- `02-serialization`과 `03-progress-isolation`: application·lifecycle FIFO lane을 count와
  byte로 함께 제한하고, lifecycle 우선 실행의 연속 상한과 yield debt를 적용했다. callback이
  application 작업을 기다리면 application lane으로 양보하며, queue가 찬 상태에서 public
  callback을 inline으로 실행하지 않는다.
- `04-completion`과 `05-relocation-continuity`: one-winner completion, tombstone와 accepted
  record 경계를 유지한다. route seal은 현재 stream frame을 버리지 않고 route availability를
  기다린 뒤 admission을 재확인한다.
- `06-routing-and-cache`: RouteMesh는 Node RID, ClientServer는 Server RID를 기준으로 후보를
  정렬한다. 후보 배열·필터·정렬은 topology revision이 바뀔 때만 만들고 send 경로는 준비된
  selection plan의 cursor를 읽는다. command 50 `messageFollow`의 source·target fence,
  hop·queue bound와 조건부 cache invalidation도 현재 source에 연결되어 있다.
- `07-dispatch-loop`와 `08-object-lifecycle`: ready-owner claim generation, count·byte·time
  수신 batch, activation admission과 Instance Spot idle candidate 검사를 적용했다. logical
  Spot timer는 runtime generation당 하나의 `ZLinkTimerScheduler`가 deadline priority queue를
  관리한다. 별도로 Spot-node idle maintenance에는 node catalog당 `PeriodicTimer`가 남아
  있으며, 이 경로의 process·churn 측정은 별도 조건이다.
- `09-session-binding`과 `11-message-ownership`: session binding gate와 actor execution gate를
  분리하고, admitted payload의 소유권을 queue로 넘긴 뒤 typed handler 경계에서 deserialize한다.
  route hold에 caller raw bytes나 sample codec을 사용하지 않는다.
- `10-liveness-and-state`: observer는 source별 최신 상태와 bounded terminal FIFO를 유지하며,
  terminal overflow를 observer별 loss envelope와 runtime metric으로 기록한다. runtime stop이나
  queue 포화만으로 stream을 닫지 않고, consumer cancellation 또는 consumer disposal에서만
  observer를 complete한다. 이로써 이전 log의 반대 서술을 정정한다.

### 아직 완료로 합산하지 않는 조건

다음은 현재 .NET local runtime의 focused source 결함이 아니라 별도 증거 또는 공통 설계가
필요한 조건이다.

1. command 50의 mixed-language process와 언어×topology 전체 process matrix를 아직 실행하지
   않았다.
2. Config 14 Instance Spot cold activation과 이전 owner route를 주입하는 `ZW-B6` operational
   harness가 없어 해당 process 항목은 withheld다. 지원되지 않은 raw route 주입이나 sample
   retry를 추가하지 않는다.
3. `D2` lock/contention, `D4` copy·parse 회계, `D6` 공통 상한 값은 측정과 독립 audit가 남아
   있다. focused unit PASS만으로 이 성능·측정 항목을 완료로 판정하지 않는다.

공개 문서 게이트도 같은 시점에 확인했다. `scripts/verify-framework-doc-contracts.sh`가
`FRAMEWORK DOC CONTRACTS CLEAN`을 출력했고, 공개 문서 트리의 `plan/` 링크와 금지된 압축
표현 검색은 모두 결과 0건이었다.

## 8. 사이트 strict build 결과

```text
mkdocs build --strict -f doc/site/mkdocs.yml
exit 1
Aborted with 140 warnings in strict mode.
```

실패 원인은 public `plan/` 링크가 아니다. `bindings/`, `common/spec/`, 기존 영어 문서와
`spec/` 아래에서 site에 포함되지 않는 상대 경로와 누락된 번역 파일을 여러 경고로 보고했다.
이 checkpoint에서는 다른 workstream의 공개 문서 구조를
임의로 고치지 않았으며, 사용자가 지정한 public `plan/` 링크 검색은 별도로 결과 0건을
확인했다. 따라서 package·runtime gate와 사이트 전체 strict gate를 하나의 PASS로 합산하지
않는다.

## 9. 후속 재검증에 따른 현재 판정

이 log의 7절에 기록한 `1499/1499`와 전체 six-sample runner exit `0`은 그 절을 작성한
시점의 evidence다. 이후 current source에서 전체 UnitTests는 `1503/1503`으로 다시 통과했고,
전체 sample runner는 TicTacToe leave completion marker에서 두 번 연속 실패했다. 단독
TicTacToe runner는 다시 통과했지만 전체 runner의 process gate를 대신하지 않는다.

현재 Config 14 runner의 명시적 결과는 다음과 같다.

```text
InstanceSpot 'all' is not executable yet.
The .NET process fixture currently covers only IS-E2E-01 through IS-E2E-03.
The aggregate runner keeps Config 14 incomplete until the remaining scenarios
have their own process evidence.
exit=2
```

현재 판정과 최신 evidence의 기준은
[`dotnet-plan-runtime-process-recheck`](20260804-dotnet-plan-runtime-process-recheck.ko.md)다.
