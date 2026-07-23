# Stateful maintenance runtime

이 문서는 RouteMesh 11.0 목표 구조를 설명한다. 현재 구현과의 차이와 완료 상태는
`framework/doc/plan/v11.0/route-mesh-11.0.0-execution-ledger.ko.md`가 소유한다.

[내부 구조 목차](README.ko.md) · [Service wire protocol](service-wire-protocol.ko.md) ·
[Location runtime](../../spec/server/40-location-runtime.ko.md) ·
[Redis Relocation Store](../../spec/server/42-relocation-store-redis.ko.md) ·
[Host 종료 계약](../../spec/server/54-graceful-drain-handoff.ko.md)

## 1. 목적과 책임 경계

이 문서는 네 service runtime이 explicit object creation, Instance reactivation, host `Retire`, Relocation Store와 recovery를
구현하는 공통 내부 구조를 정의한다. Application public API는 logical identity, opaque byte relocation adapter와 host
operation만 표현한다. Store version, owner lease, target reservation, relocation phase와 replay cursor는 runtime
내부에 둔다.

Location Store는 owner·location·generation, phase, `RelocationId`, source·target fence, Relocation root
reference·checksum, authoritative participant set·membership, placement reservation, aggregate generation·commit과
replay·completion count를 한 expected-version transaction domain에서 관리한다. Relocation Store는 application state,
accepted journal, participant별 state·journal과 replay payload를 immutable root로 관리한다. 두 provider 사이에는
distributed transaction과 2PC가 없고 같은 Redis deployment 또는 서로 다른 Redis를 사용할 수 있다.

Object Server factory에 `Recreate` 또는 `Snapshot`이 하나라도 있으면 startup snapshot은 Relocation Store가 정확히
하나인지 socket bind 전에 검증한다. `Disabled` cross-node 이동은 capture 전에 거부한다. Same-node Actor join은
Location membership transaction만 사용하고 Relocation Store를 호출하지 않는다. Creation intent는 logical create의
Location reservation domain에 남으며 Relocation Store 등록 조건을 만들지 않는다.

## 2. Host maintenance barrier

Host마다 maintenance barrier 하나가 다음 operation의 순서를 정한다.

- Actor·Spot create, Actor join과 Instance placement
- inbound relocation reservation과 session binding
- application timer와 handler admission
- `Retire` inventory·intent publication·unit permit과 `Shutdown` seal

`Retire` preflight는 state를 바꾸기 전에 모든 local stateful object, accepted work, session과 진행 중인
infrastructure operation을 inventory한다. Target eligibility와 Relocation Store capability를 준비할 수 없거나 preflight
deadline이 끝나면 reservation을 정리하고 `Blocked`를 반환한다. Host state와 application admission은 그대로다.

Preflight가 성공하면 host를 `Retiring`으로 게시하고 새 placement·membership·inbound relocation target을 닫는다.
Coordinator는 User Spot aggregate, standalone Actor와 Instance Spot queue에 infrastructure intent notification을
예약한다. Application callback이나 public readiness API는 없다. Notification을 처리한 turn 경계에서 outbound,
target inbound, 필요한 `Capture`·`Restore` callback과 예상 payload byte permit을 nonblocking으로 모두 얻은 unit만
reversible하게 seal한다. Permit을 얻지 못하면 notification을 다시 예약하고 application turn을 계속 처리한다.

모든 unit이 source dispatch에서 분리된 뒤 host를 `Draining`으로 전환한다. 첫 relocation commit 전 failure는 모든
tentative 작업을 abort하고 `Serving`을 복원할 수 있다. 첫 commit 뒤 deadline이나 relocation failure는 bounded
teardown과 `ForceStopped`로 끝낸다. Connection-bound accepted work를 `Captured` 전에 terminal drain하지 못하면 같은
precommit abort 규칙을 적용한다.

Concurrent `Retire`와 `Shutdown`은 같은 barrier에서 먼저 확정된 operation에 합류한다. Observer와 metric reader는
barrier 진행을 막는 claim을 갖지 않는다.

## 3. Authority와 local admission fence

Store-backed object coordinator는 provider가 발급한 값을 역할별로 분리한다.

| 값 | 역할 |
|---|---|
| `StoreVersion` | expected-version CAS와 read snapshot 구분 |
| `ObjectGeneration` | delete 뒤 같은 canonical key로 새로 만든 object 구분 |
| `AuthorityOwnerGeneration` | 같은 object의 authority owner 변경 순서 |
| `OwnerLeaseGeneration` | current host process owner lease 구분 |
| `TargetAttemptGeneration` | 같은 relocation에서 target reservation 교체 구분 |

Host owner lease token은 process 전체가 공유한다. 기본 profile은 renew interval 5초, TTL 15초, renew timeout 3초,
fencing margin 5초다. Startup은 다음 strict relation을 만족해야 한다.

```text
renewInterval + renewTimeout < ttl - fencingMargin
```

Store read를 시작한 local monotonic 시각을 `requestStart`로 저장하고 provider가 반환한 `StoreNow`와 lease expiry를
다음처럼 local admission deadline으로 변환한다.

```text
remaining = max(0, leaseExpiryStore - storeNow - fencingMargin)
localDeadline = requestStart + remaining
```

응답을 받은 local 시각으로 deadline을 연장하지 않는다. Deadline이 끝났거나 object·owner·host fence가 current
authority와 다르면 message, timer, reply completion과 CAS write를 거부한다.

## 4. Explicit creation과 reactivation

Actor와 User·Instance Spot은 manager의 explicit `Create`·`GetOrCreate`만 생성 intent를 만든다. Generic
`Reserve`는 object kind, global ActorId, global SpotRid, stable type, target descriptor, capacity delta와 fence를 원자적으로
기록한다. `NewObject` CAS는 final `ObjectGeneration`, `AuthorityOwnerGeneration`과 `Creating` row를 만든다.
Factory·initialize와 initial membership이 끝난 뒤 같은 fence로 reservation을 commit하고 `Ready`를 publish한다.
Manager `Find`와 ID-only messaging은 `Ready`만 사용한다. Entry Spot은 host startup 중 initialize하며 caller가
생성하지 않는다.

Factory failure는 local barrier를 failed 상태로 seal한다. Waiting request는 한 번만 terminal 처리하고 one-way
operation은 drop event를 남긴다. Coordinator는 exact Store version, object·owner generation과 owner lease로 row를
삭제한다. Delete 결과가 불명확하면 read로 reconcile하며 `Missing`을 확인할 때까지 local registry를 failed로
유지한다. 그 다음 caller만 새 `NewObject`를 시작한다.

Creation intent는 최대 1 MiB request의 content reference와 hash, placement input을 durable하게 보존한다. Target
host의 initial scan과 bounded background scan은 자신이 소유한 `Creating` intent를 재개한다. Instance runtime이
사라져도 같은 stored intent로 reactivation하며 normal message에서 type, Mesh와 placement를 재구성하지 않는다.
Reservation은 TTL을 사용하지 않고 Creating authority와 owner lease로 복구한다. 실패하면 exact `Abort`로 pending
capacity를 회수한다.

Object `Client`와 `Server` role은 Location Store를 요구한다. Object `None` role은 manager, factory와 hidden local
object runtime을 만들지 않는다.

## 5. Object coordinator

Actor와 Instance Spot relocation은 공통 coordinator loop를 사용한다. Object별 factory, membership callback, state
adapter와 cleanup은 adapter가 흡수한다.

```text
Read current authority
          |
          v
Validate exact owner, lease, phase and deadline
          |
          v
Run retry-safe preparation
          |
          v
CompareExchange with expected StoreVersion
          |
          +---- conflict ----> Read and classify current state
          |
          v
Run the post-commit action once per local barrier
```

Coordinator는 provider가 반환한 expected `StoreVersion` 없이 authority를 쓰거나 지우지 않는다. Conflict에서 이전
payload를 덮어쓰지 않고 current snapshot을 다시 분류한다. Application callback은 crash와 target replacement에서
다시 실행될 수 있으므로 retry-safe해야 하며 Framework는 외부 side effect의 exactly-once를 보장하지 않는다.

## 6. Permit, capture와 durability boundary

Process gate의 기본값은 active outbound·inbound unit 각각 64, concurrent `Capture`·`Restore` 각각 8, encoded
payload in-flight 256 MiB다. 세 gate는 독립적이며 모두 queue seal 전에 예약한다. 하나라도 즉시 얻지 못하면
seal하지 않는다. 단일 unit의 encoded payload가 256 MiB를 넘으면 payload window가 빈 상태에서만 oversized unit
하나로 진행한다.

Reversible seal이 participant별 accepted boundary를 고정하면 source lifetime별로 work를 분류한다.

- `leaseBacked` source의 accepted record만 durable frozen journal에 기록한다. 각 record는 exact source node
  lifecycle, `OwnerId`와 `OwnerLeaseGeneration`을 보존한다.
- `connectionBound` source의 accepted send·request와 모든 bound-session request는 `Captured` 전에 terminal
  state까지 drain한다. 이 record를 relocation envelope에 넣지 않는다.
- Drain이 deadline 안에 끝나지 않으면 pre-Captured abort, `Blocked/DeadlineExceeded`와 source admission 복원으로
  끝낸다.

Coordinator는 seal 시점에 실행하지 않은 application message queue, accepted journal, timer logical
registration·pending tick, optional application state, manifest와 Framework metadata를 deterministic relocation
stream으로 encode한다. Snapshot state는 adapter가 반환한 opaque bytes이며 Framework는 byte count와 checksum만
검증한다. `Recreate`도 application state section만 없는 complete relocation envelope을 만든다. Native timer
handle과 callback continuation은 포함하지 않는다.

Seal 뒤 ingress는 frozen payload에 추가하지 않고 bounded source hold에 둔다. Precommit abort 뒤 steady source
normalization이 끝나면 hold를 source queue에 arrival order로 복원한다. Owner commit 뒤에는 original operation ID,
generation과 reply route를 유지해 target ingress로 relay한다.

Immutable chunk, root manifest 순서로 저장한 뒤 authority를 `Captured`로 CAS한다. 이 CAS가 durability boundary다.
Source가 그 전에 종료되면 relocation을 abort하고 continuity replay를 주장하지 않는다. CAS에 연결되지 않은 object는
orphan이며 provider retention으로 정리한다.

Relocation root retention은 24시간이고 renew threshold는 12시간이다. `Captured`와 `Prepared` CAS 직전에 complete tree의
remaining retention을 확인하거나 renew한다. Target은 current authority가 가리키는 root만 읽고 checksum을
검증한다.

Completion을 append할 때도 새 immutable root를 먼저 저장하고 검증한 뒤 Location authority의 reference, checksum과
count를 한 CAS로 교체한다. Conflict loser의 root는 orphan이다. Cleanup은 source·relay·steady normalization gate를
완료하고 Location authority에서 reference를 release한 뒤에만 Relocation root를 삭제한다.

Published reference의 permanent missing, checksum mismatch 또는 participant inventory digest mismatch는
non-retriable `RelocationDataLost`다. Runtime은 authority와 application admission을 error로 seal하며 commit된 owner와
membership을 source로 rollback하지 않는다.

## 7. Relocation phase와 target replacement

Stable `RelocationId`는 relocation root, journal과 terminal completion을 묶는다. Runtime은 non-zero 128-bit CSPRNG 값을
만들고 active relocation과 retained root의 충돌을 검사한다. `TargetAttemptGeneration`은 target을 교체할 때만
증가하며 stable relocation identity와 relocation root를 바꾸지 않는다.

| Phase | Main owner와 완료 조건 |
|---|---|
| `Preparing` | source admission seal과 accepted boundary가 고정됨 |
| `Captured` | source가 complete relocation root를 authority에 연결함 |
| `Prepared` | source와 exact target attempt·reservation이 고정됨 |
| `Committed` | target이 current owner가 됨 |
| `Activating` | owner commit 뒤 target lifecycle callback과 journal replay가 진행 중임 |
| `Activated` | target callback과 replay가 끝났지만 admission은 sealed임 |
| `Cleaning` | durable source cleanup이 진행 중임 |
| `Completed` | activation과 source cleanup이 terminal이며 route barrier를 이어서 처리함 |
| `Aborted` | commit 전 relocation이 끝나고 source normalization을 이어서 처리함 |

각 phase transition은 expected `StoreVersion` CAS다. `Committed` 뒤 source owner로 rollback하지 않는다. Target
replacement는 exact reservation handshake 뒤 target attempt, target owner lease와 reservation만 바꾼다. Old target의
late callback은 current attempt와 authority read를 통과하지 못한다.

## 8. Aggregate membership과 session barrier

User Spot과 member Actor를 함께 이동할 때 non-zero 128-bit aggregate ID와 exact participant inventory를 사용한다.
Participant는 최대 1024개이고 encoded aggregate는 최대 1 MiB다. User Spot이 있다는 이유만으로 Retire를 차단하지
않는다. 모든 factory의 명시적 maintenance policy, target capacity와 state compatibility를 preflight한다.

Location Store의 bounded canonical participant set, participant별 mutation, aggregate generation과 inventory
digest가 authority다. Relocation manifest는 participant별 payload를 찾기 위해 같은 digest를 보존하지만 participant
authority가 아니다. 두 digest가 일치하지 않으면 target restore를 시작하지 않는다.

Target reservation은 Spot과 member Actor의 global identity, ObjectGeneration과 kind를 고정한다. `Committed` CAS는
aggregate owner와 membership visibility를 원자적으로 바꾼다. Target은 commit 전에 factory·restore와 journal
validation·staging을 끝내고 commit 뒤 joined callback, message·journal replay와 logical timer 복원을 실행한다.
Timer scheduler는 새 native handle을 만들고 pending tick을 frozen ordering boundary에 넣는다. Application
`Restore`는 Framework timer를 다시 등록하지 않는다. Source leave callback과 old membership removal은 durable
source cleanup에 포함한다.

Physical STREAM connection은 이동하지 않는다. Session owner는 ingress를 reversible하게 seal하고 high-water를
source에 전달한다. Target restore와 replay가 high-water까지 끝나도 route를 즉시 바꾸지 않는다. `Completed` CAS 뒤
session owner가 binding route를 atomic하게 바꾸고 routed ACK를 보내며, target은 steady authority normalization 뒤
application admission을 연다.

`Activated`, `Cleaning`과 `Completed`에서는 target이 계속 sealed 상태다. `Completed`만으로 manager `Find`의 Ready projection을
publish하지 않는다. Abort도 source route ACK와 steady source normalization 뒤에 admission을 복원한다.

## 9. Replay와 request completion

Journal entry는 participant ID와 non-zero sequence를 사용한다. Sequence는 1부터 증가하며 accepted boundary를 넘을
수 없고 wrap하지 않는다. Duplicate record는 canonical bytes가 같을 때만 idempotent하다.

`OperationId`와 `ReplyRouteId`는 source owner lifecycle 안에서 각각 unique하고 wrap·reuse하지 않는다. Operation ID는
deduplication에 사용하고 reply routing에는 original `ReplyRouteId`를 사용한다. Durable terminal identity는 stable
`RelocationId`와 `OperationId`다.

Target은 handler 결과와 delivery state를 새 immutable relocation root에 기록하고 authority CAS로 root, terminal count와
pending relay count를 함께 바꾼다. Source의 authenticated `replyRelayAck` 또는 exact request-source owner lease
expiry가 확인되기 전에는 pending relay를 완료하지 않는다. Physical disconnect는 terminal delivery 증거가 아니다.

External side effect 뒤 relocation root CAS 전에 process가 종료되면 handler가 같은 operation ID로 다시 실행될 수 있다.
Application은 operation ID 또는 자신의 durable key로 side effect를 idempotent하게 처리해야 한다.

## 10. Cleanup과 recovery

Durable source cleanup은 source callback·scope, old Entry membership, participant와 session state를 정리한 결과를
authority에 기록한다. `relocationComplete`는 이 state가 terminal임을 확인한 뒤 target finalization을 한 번 알린다.
Current source lease가 끝났으면 recovery coordinator가 exact immutable source token을 확인하고
`SourceLeaseExpired`를 기록할 수 있다.

Host deadline에 도달하면 local resource를 bounded teardown하지만 committed relocation을 source로 rollback하지
않는다. Current authority와 relocation root가 남아 있으면 다른 coordinator가 recovery를 이어간다. Cleanup과
reservation release는 반복 실행해도 같은 terminal state로 수렴해야 한다.

## 11. Route cache와 stale result

Location cache는 global ActorId·SpotRid, current route, StoreVersion, object·owner generation, owner lease와 local
monotonic deadline을 immutable snapshot으로 저장한다. `Ready` positive result만 최대 `RouteCacheMaxAge` 동안
보관하고 negative cache는 두지 않는다. Store recovery, higher authority, lease expiry, handover와 explicit stale
result는 즉시 invalidate한다.

Relocation mapping은 최대 8 hop을 따라가며 original operation ID, generation, payload와 reply route를 보존한다.
Forwarding queue는 최대 1024 message 또는 16 MiB다. 실패한 operation을 다른 owner에 새 operation으로 숨겨서
재제출하지 않는다. `ActorRef`와 `SpotRef`는 immutable location snapshot이므로 exact-ref mutation은 fresh
incarnation으로 자동 retarget하지 않는다.

## 12. 검증

- Barrier inventory, create·join과 신규 admission이 한 순서로 정렬된다.
- Retire intent notification이 application callback을 호출하지 않고 permit 실패 시 queue를 seal하지 않는다.
- Outbound·inbound 64, `Capture`·`Restore` 8과 payload 256 MiB 기본 gate가 독립적으로 제한한다.
- Factory 실패와 ambiguous delete가 새 owner를 잘못 Ready로 publish하지 않는다.
- Connection-bound work가 durable journal에 포함되지 않는다.
- Frozen payload가 미실행 message, accepted journal과 logical timer·pending tick을 포함한다.
- Seal 뒤 ingress hold가 abort 뒤 source queue 또는 commit 뒤 target relay 중 하나로 정리된다.
- `Captured` CAS 전 crash에서 relocation replay를 시작하지 않는다.
- Target replacement가 stable RelocationId와 relocation root를 바꾸지 않는다.
- Actor owner와 target Entry Spot membership이 같은 commit에서 바뀐다.
- `Activated`부터 route ACK·steady normalization 전까지 target admission이 닫혀 있다.
- Request terminal completion과 relay ACK가 같은 durable identity로 한 번만 수렴한다.
- Preflight와 첫 commit 전 failure는 admission을 복원하고, 첫 commit 뒤 failure는 `ForceStopped`로 끝난다.
