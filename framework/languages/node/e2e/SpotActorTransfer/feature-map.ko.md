# Node.js SpotActorTransfer E2E feature map

공통 정본은 [Config 10 — Spot·Actor relocation](../../../../doc/framework/common/e2e/config-10-spot-actor-relocation.ko.md)이다.
consumer는 Node HTTP client wrapper로 역할 서버 endpoint를 호출하고, bound session 검증에는 stream
connector를 사용한다. 아래 표는 정식 시나리오 ID를 한 행씩 기록한다.

| 시나리오 | 상태 | 검증 근거 |
|---|---|---|
| `ST-A1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: local admission accept와 callback 순서. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-A2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: local admission reject의 무효과. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-A3` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: joined callback 완료 전 packet dispatch 차단. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-B1` | 부분 구현 | Public object placement로 source와 다른 target Spot을 선택하고 native deferred Join을 실행한다. Seal 중 수락한 packet을 source handoff backlog에 넣고 target mailbox에서 callback 전에 replay한다. Target admission, durable deferred-completion root staging, state restore, `Joined`, backlog handler, commit ACK, `onJoinCompleted(Accepted)`와 새 owner request 순서를 확인한다. Join 완료 뒤 public `ActorManager.find`로 target authority와 보존된 object generation도 확인한다. 최신 증거: `log/20260729-171629-4066922`. 일반 backlog와 application state를 포함하는 restart recovery manifest 및 실제 target process restart 증거는 아직 없다. |
| `ST-B2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: commit 뒤 source cleanup. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-B3` | 전환 필요 | 현재 runner는 transfer adapter가 없는 actor의 기본 빈 state transfer 성공과 target 기본 state를 확인한다. 그러나 `joined -> location_committed`를 성공 순서로 단언해, 공통 시나리오의 `location_committed -> joined` 순서와 다르다. 이 순서를 정렬하기 전에는 기존 Track A~E 로그를 완료 증거로 사용하지 않는다. |
| `ST-B4` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: custom empty state transfer. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-C1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: commit 전 source 종료. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-C2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: commit 뒤 source 종료. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-C3` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: callback 단계별 failure 분류. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-D1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: location commit 공개 시점. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-D2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: stale generation fencing. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-E1` | 구현 | Public `SpotId`, `find`, `defer`와 completion callback을 사용한다. Native session bind가 확정한 session 좌표와 binding generation을 transfer snapshot에 보존하고 target native registry에 같은 generation fence로 복원한다. Source와 다른 target을 강제한 실제 process에서 transfer 전 push, durable commit ACK와 `onJoinCompleted(Accepted)`, transfer 후 같은 session의 push를 검증했다. 증거: `log/20260725-092454-2307879`. |
| `ST-E1A` | 부분 구현 | `internal route refresh preserves object generation while explicit bind can replace an incarnation`과 SM-D4A focused runner가 same-generation internal refresh, 새 generation explicit bind와 이전 token 격리를 검증한다. Ownership command가 durable `Completed` 뒤에만 발행되는 순서는 process relocation runner에서 추가 검증해야 한다. |
| `ST-E2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: transfer 실패 때 기존 session binding 유지. Track A~E 로그: `log/20260710-152609-2661347`. |
| `ST-F1` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: moving backlog FIFO. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-F2` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: location publish 전 replay 순서. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-F3` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: bound session의 cross-move FIFO. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-F3A` | 미구현 | Session owner pause와 owner lease fence를 실제 process에서 검증하는 시나리오가 없다. |
| `ST-F4` | 구현 | Relocation 전에 resolve한 one-way와 request를 internal delivery gate에서 보류한다. Commit 뒤 같은 immutable operation을 각각 두 번 제출해도 relay, final owner handler와 request terminal은 각각 한 번이다. Relay evidence는 128-bit operation ID, ObjectGeneration, deadline, correlation, reply route, checksum과 authority generation 증가를 확인한다. Duration 뒤 해제한 request는 handler admission 전에 stale로 끝난다. Fresh 증거 `log/20260728-104100-140600`, stderr 0 bytes다. |
| `ST-F5` | 구현 | A→B→C relocation 동안 보류한 one-way와 request가 두 Message Follow route를 순서대로 거쳐 final owner에서 각각 한 번 처리된다. 두 hop의 operation ID, ObjectGeneration, deadline, correlation, reply route와 checksum은 같고 authority generation은 `1→2→3`, hop count는 `1→2`다. Expiry 뒤 request는 세 owner의 handler를 실행하지 않는다. Fresh 증거 `log/20260728-104100-140606`, stderr 0 bytes다. |
| `ST-F6` | 구현 | 실제 callback, location 또는 stream evidence로 검증한 대상: request correlation과 caller timeout 뒤 late reply 격리. Track F 로그: `log/20260710-200221-3864800`. |
| `ST-G1` | 미구현 | SpotWide·PerActor의 yielded continuation과 모든 실행 lane을 포함한 relocation barrier E2E가 없다. |
| `ST-G2` | 미구현 | 큰 participant inventory와 typed capacity aggregate all-or-none E2E가 없다. |
| `ST-G3` | 미구현 | PerActor Spot authority 선전환과 Actor별 source·target route 분할 E2E가 없다. |
| `ST-G4` | 미구현 | relocation 중 `ToActor` Message Follow와 target queue 순서를 검증하는 E2E가 없다. |
| `ST-G5` | 미구현 | Entry·PerActor Actor relocation interruption 목표와 초과 뒤 계속 진행을 검증하는 E2E가 없다. |
| `ST-G6` | 미구현 | `ApplicationSignaled` readiness와 completion callback의 source·target owner를 검증하는 E2E가 없다. |
| `ST-H1` | 미구현 | Deferred Join 등록, immutable request와 Actor queue barrier를 실제 process에서 검증하지 않는다. |
| `ST-H2` | 미구현 | Join completion outcome, operation ID와 crash recovery E2E가 없다. |
| `ST-H3` | 미구현 | Context identity와 relocation 이후 source fence E2E가 없다. |
| `ST-H4` | 미구현 | 허용 execution context, 중복 등록과 relocation error parity E2E가 없다. |
| `ST-H4A` | 미구현 | Deferred Join 등록량·payload·timeout 경계와 Relocate·Shutdown race E2E가 없다. |
| `ST-H4B` | 미구현 | Join 뒤 Yield, awaited cycle과 reply terminal E2E가 없다. |
| `ST-H5` | 미구현 | MessageContext와 Actor handler signature parity를 실제 transport로 검증하는 E2E가 없다. |
| `ST-I1` | 미구현 | 실제 encoded Actor·Spot payload profile과 경계·초과 크기 E2E가 없다. |
| `ST-I2` | 미구현 | 다량 Recreate·Snapshot Actor relocation의 처리 시간과 Actor·control service 연속성 E2E가 없다. |
| `ST-I3` | 미구현 | 다량 Instance Spot·SpotWide relocation의 처리 시간과 Spot·Actor·control service 연속성 E2E가 없다. |
| `ST-I4` | 미구현 | Actor·Spot × one-way·request × commit 전·후 Message Follow matrix가 없다. |
| `ST-I5` | 미구현 | Message Follow 기간 종료, duplicate, deadline, generation, loop와 bound E2E가 없다. |
| `ST-I6` | 미구현 | Actor·Spot multi-hop relocation과 Message Follow route 정리 E2E가 없다. |

## 증거 경계

- callback과 transfer 순서는 actor node가 실제 callback에서 남긴 evidence를 transfer id로 연결한다.
- location commit은 Redis key를 직접 읽지 않고 location monitoring event의 row update로 확인한다.
- bound session은 stream connector가 받은 push의 actor id, node rid와 state version으로 확인한다.
- Track F는 source backlog, target enqueue와 location commit 순서를 actor별 arrival index로 대조한다.
- request-correlation 시나리오는 request sequence와 flags를 보존하고, caller timeout 뒤 late reply가
  다음 request를 방해하지 않는지 확인한다.
- Focused contract test는 cross-node Accepted root의 operation ID·raw reply·target ActorRef·generation과
  `Prepared→Committed→Delivered` cursor, callback retry·dedupe, backlog 선행 순서를 검증한다. 새 journal
  instance는 provider-backed committed root를 복구하고 Delivered 뒤 callback을 다시 실행하지 않는다.
  Delivered를 기록한 뒤에는 authority에서 root reference를 CAS로 해제하고 blob을 삭제한다. 이 해제 CAS가
  충돌해도 callback은 다시 실행하지 않고 cleanup만 재시도한다. Prepared 뒤 materialization 실패 cleanup과
  callback 뒤 Delivered CAS conflict도 검증한다. Redis provider
  test는 immutable bytes, CRC32C와 provider clock 기준 expiry·renew·delete를 검증한다.
- `run_e2e.sh all`은 일반 네 process 묶음과 세 Actor node가 필요한 `ST-F5`를 별도 fresh process로
  실행한다. 두 시나리오는 application metadata나 DTO에 operation ID·ActorRef·owner RID를 노출하지
  않고, runtime construction에 주입한 delivery gate로 resolve와 submit 사이만 지연한다.
- ActorNode는 actor-local handler registry 대신 Nest의 Spot·Entry Spot handler registration을
  사용하고, 제거된 Location row event monitoring을 더 이상 참조하지 않는다. ST-B1은 Join 완료 뒤
  public `ActorManager.find` 결과로 target authority와 보존된 object generation을 확인한다. Exact
  authority commit 시점은 별도 internal conformance evidence를 연결해야 한다. 실제 process restart는
  아직 완료 증거가 아니다. Target Actor state, transfer state와 handoff backlog가 process memory와 Join
  wire에만 있고 startup Actor recovery coordinator가 없기 때문이다.
- Redis Store는 첫 병렬 사용을 하나의 connection attempt로 직렬화하고 `isReady` 뒤 command를
  제출한다. Location·Relocation Store는 같은 Redis deployment에서 `:location`과 `:relocation`
  prefix를 사용한다. Authority·object generation·node capacity·creation terminal은 각각 독립 key로
  저장하며 reserve·commit·complete는 관련 row만 조건으로 묶는 bounded batch를 사용한다. Provider
  전체 상태를 하나의 record로 저장하지 않는다. Redis contract를 포함한 focused test 57/57과 Node
  workspace build가 통과했다.
- Provider-backed relocation은 target capacity reservation을 opaque Location Store row로 저장하고,
  source owner·node lifecycle·capacity fence를 확인한 뒤 authority와 source·target capacity를 하나의
  conditional batch로 변경한다. Accepted journal이 같은 authority payload를 preserve 갱신해도 owner와
  allocation fence가 유지되면 최신 payload를 보존해 `newOwner` CAS를 수행한다. ObjectGeneration은
  relocation 전후 유지하고, stale 판정은 generation만이 아니라 이전 물리 node까지 함께 확인한다.
- `log/20260728-065314-1321351`의 ST-F4는 non-public DI boundary의 internal delivery gate로 resolve된
  이전 물리 delivery를 지연한다. G1은 target에서 정확히 한 번 처리되고 G2는
  `actorLocationStale`로 끝나며 application handler가 실행되지 않는다. Runtime marker는 setup
  대기에만 사용하고 완료 판정은 public terminal, application handler count와 fixture capture·release
  count로 수행한다.
- `log/20260728-071841-1562392`의 ST-F5는 source Actor shell을 Core leave 뒤 Framework registry에서
  제거하고, 이미 `Delivered`인 durable Join root를 다음 Join operation이 CAS로 교체하도록 검증한다.
  Message Follow stale fence는 ObjectGeneration뿐 아니라 이전 physical NodeRid도 보존한다. 따라서
  generation을 유지하는 A→B→C relocation에서도 A와 B route가 순서대로 사용되고 expiry 뒤 처음
  resolve한 A의 ActorRef가 `actorLocationStale`로 끝난다.
- `log/20260728-104100-140600`과 `log/20260728-104100-140606`은 one-way뿐 아니라
  positive request도 같은 delivery gate에서 보류한다. ST-F4는 같은 immutable operation을 두 번
  제출해 dedupe 뒤 relay, final handler와 terminal이 한 번인지 확인한다. ST-F5는 이전 두 owner가
  기록한 relay context를 비교해 operation identity, deadline, reply correlation, checksum과
  ObjectGeneration을 유지하고 authority generation과 hop count만 증가하는지 확인한다. 두 실행의
  모든 stderr는 0 bytes다.
- SpotActorTransfer 전용 readiness는 제거된 Location row API가 아니라 실행 중인 모든 host의 public
  `ZLinkRouteMeshRuntime.snapshot()`과 `ZLinkLocationRuntimeQuery.listTopology()`를 조회한다.
  Automatic RouteMesh는 process-local legacy peer row 대신 Redis의 live MeshNode descriptor에서
  endpoint와 lifecycle generation을 읽고, RID가 작은 process만 연결을 시작한다. Opaque provider에는
  cross-process change stamp가 없으므로 authoritative descriptor scan을 polling한다. Raw backend의
  `serving` peer를 public `ready` state로 투영하도록 맞췄고 admission reply가 일시적으로 제출되지
  않아도 process를 종료하지 않는다. Backend·auto-connect focused test 51/51이 통과했다.
- `ST-F3A`, `ST-G1~G6`, `ST-H1~H5`와 `ST-I1~I6`은 아직 실행하지 않는다.
