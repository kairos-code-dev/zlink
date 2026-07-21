# Stateful maintenance runtime

[내부 구조 목차](README.ko.md) · [Service wire protocol](service-wire-protocol.ko.md) ·
[Location runtime](../../spec/server/40-location-runtime.ko.md) ·
[Host 종료 계약](../../spec/server/54-graceful-drain-handoff.ko.md)

## 1. 목적과 책임 경계

이 문서는 네 service runtime이 local creation, Instance cold activation, host `Retire`, checkpoint와 recovery를
구현하는 공통 내부 구조를 정의한다. Application public API는 logical identity, typed state adapter와 host
operation만 표현한다. Store version, owner lease, target reservation, transfer phase와 replay cursor는 runtime
내부에 둔다.

## 2. Host maintenance barrier

Host마다 maintenance barrier 하나가 다음 operation의 순서를 정한다.

- Actor·Spot create, Actor join과 Instance placement
- inbound transfer reservation과 session binding
- application timer와 handler admission
- `Retire` inventory, reversible admission seal과 `Shutdown` seal

`Retire` preflight는 state를 바꾸기 전에 모든 local stateful object, accepted work, session과 진행 중인
infrastructure operation을 inventory한다. Target eligibility와 checkpoint capability를 준비할 수 없거나 preflight
deadline이 끝나면 reservation을 정리하고 `Blocked`를 반환한다. Host state와 application admission은 그대로다.

Preflight가 성공하면 barrier 안에서 reversible admission seal과 participant별 accepted boundary를 고정한다.
Connection-bound accepted work를 `Captured` 전에 terminal drain하지 못해도 transfer를 abort하고 admission을
복원한다. 모든 object가 checkpoint와 target reservation을 준비한 뒤 host를 `Draining`으로 전환하면 seal은 더
이상 되돌리지 않는다. 이후 deadline이나 transfer 실패는 bounded teardown과 `ForceStopped`로 끝낸다.

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
| `TargetAttemptGeneration` | 같은 transfer에서 target reservation 교체 구분 |

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

## 4. Local creation과 cold activation

Store-backed Actor와 User Spot은 `NewObject` CAS로 final `ObjectGeneration`, `AuthorityOwnerGeneration`과 `Creating`
row를 먼저 만든다. Actor factory·initialize·initial Entry Spot membership 또는 User Spot factory·initialize가 모두
끝난 뒤 같은 fence로 `Ready`를 commit한다. Remote resolver와 messaging은 `Ready`만 사용한다. Entry Spot은 host
startup 중 initialize하고 이를 publish한 뒤에만 host가 `Serving`이 된다.

Factory failure는 local barrier를 failed 상태로 seal한다. Waiting request는 한 번만 terminal 처리하고 one-way
operation은 drop event를 남긴다. Coordinator는 exact Store version, object·owner generation과 owner lease로 row를
삭제한다. Delete 결과가 불명확하면 read로 reconcile하며 `Missing`을 확인할 때까지 local registry를 failed로
유지한다. 그 다음 caller만 새 `NewObject`를 시작한다.

Instance cold activation은 source가 target을 선택한 뒤 outbound transport admission 전에 `ColdActivating`
`NewObject` CAS를 수행한다. Target은 다시 claim하지 않고 current authority를 exact 확인해 factory·initialize와
`Ready` CAS를 수행한다. Target host의 initial scan과 bounded background scan은 자신이 소유한 `ColdActivating`
intent를 재개한다. Scan과 늦은 submit은 같은 local activation barrier로 수렴한다. Lost submit의 application
payload를 scan이 다시 실행하지 않는다.

Store-less object는 runtime-local opaque authority만 가진다. 이 mode는 same-process messaging 외의 remote resolve,
distributed join, transfer와 session binding에 사용하지 않는다.

## 5. Object coordinator

Actor와 Instance Spot transfer는 공통 coordinator loop를 사용한다. Object별 factory, membership callback, state
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

## 6. Capture와 durability boundary

Reversible seal이 participant별 accepted boundary를 고정하면 source lifetime별로 work를 분류한다.

- `leaseBacked` source의 accepted record만 durable frozen journal에 기록한다. 각 record는 exact source node
  lifecycle, `OwnerId`와 `OwnerLeaseGeneration`을 보존한다.
- `connectionBound` source의 accepted send·request와 모든 bound-session request는 `Captured` 전에 terminal
  state까지 drain한다. 이 record를 checkpoint에 넣지 않는다.
- Drain이 deadline 안에 끝나지 않으면 pre-Captured abort, `Blocked/TransferDisabled`와 source admission 복원으로
  끝낸다.

Coordinator는 accepted journal과 optional application state를 deterministic checkpoint stream으로 encode한다.
Snapshot state는 `framework-json-v1`로 검증하고 원본 bytes를 보존한다. `Recreate`도 state section이 없는 complete
checkpoint envelope을 만든다.

Immutable chunk, root manifest 순서로 저장한 뒤 authority를 `Captured`로 CAS한다. 이 CAS가 durability boundary다.
Source가 그 전에 종료되면 transfer를 abort하고 continuity replay를 주장하지 않는다. CAS에 연결되지 않은 object는
orphan이며 provider retention으로 정리한다.

Checkpoint retention은 24시간이고 renew threshold는 12시간이다. `Captured`와 `Prepared` CAS 직전에 complete tree의
remaining retention을 확인하거나 renew한다. Target은 current authority가 가리키는 root만 읽고 checksum을
검증한다.

## 7. Transfer phase와 target replacement

Stable `TransferId`는 checkpoint, journal과 terminal completion을 묶는다. Runtime은 non-zero 128-bit CSPRNG 값을
만들고 active transfer와 retained root의 충돌을 검사한다. `TargetAttemptGeneration`은 target을 교체할 때만
증가하며 stable transfer identity와 checkpoint를 바꾸지 않는다.

| Phase | Main owner와 완료 조건 |
|---|---|
| `Preparing` | source admission seal과 accepted boundary가 고정됨 |
| `Captured` | source가 complete checkpoint root를 authority에 연결함 |
| `Prepared` | source와 exact target attempt·reservation이 고정됨 |
| `Committed` | target이 current owner가 됨 |
| `Activating` | target factory·restore가 진행 중임 |
| `Activated` | target replay가 끝났지만 admission은 sealed임 |
| `Cleaning` | durable source cleanup이 진행 중임 |
| `Completed` | activation과 source cleanup이 terminal이며 route barrier를 이어서 처리함 |
| `Aborted` | commit 전 transfer가 끝나고 source normalization을 이어서 처리함 |

각 phase transition은 expected `StoreVersion` CAS다. `Committed` 뒤 source owner로 rollback하지 않는다. Target
replacement는 exact reservation handshake 뒤 target attempt, target owner lease와 reservation만 바꾼다. Old target의
late callback은 current attempt와 authority read를 통과하지 못한다.

## 8. Actor membership과 session barrier

Retire 대상 Actor는 source Entry Spot의 current member여야 한다. User Spot member Actor가 있으면 preflight를
`Blocked/TransferDisabled`로 끝내고 membership과 admission을 바꾸지 않는다. User Spot normal `Close`도 Actor
membership이 남아 있으면 public `false`를 반환한다. Runtime이 Actor를 숨겨서 leave 또는 destroy하지 않는다.

Target capacity offer는 initialized target Entry Spot RID, object generation과 kind를 고정한다. `Committed` CAS는
Actor owner, `AuthorityOwnerGeneration`과 current target Entry Spot membership을 atomic하게 바꾼다. Target은
factory·restore, target Entry Spot joined callback, journal replay 순서로 진행한다. Source leave callback과 old Entry
membership removal은 durable source cleanup에 포함한다.

Physical STREAM connection은 이동하지 않는다. Session owner는 ingress를 reversible하게 seal하고 high-water를
source에 전달한다. Target restore와 replay가 high-water까지 끝나도 route를 즉시 바꾸지 않는다. `Completed` CAS 뒤
session owner가 binding route를 atomic하게 바꾸고 routed ACK를 보내며, target은 steady authority normalization 뒤
application admission을 연다.

`Activated`, `Cleaning`과 `Completed`에서는 target이 계속 sealed 상태다. `Completed`만으로 resolver Ready를
publish하지 않는다. Abort도 source route ACK와 steady source normalization 뒤에 admission을 복원한다.

## 9. Replay와 request completion

Journal entry는 participant ID와 non-zero sequence를 사용한다. Sequence는 1부터 증가하며 accepted boundary를 넘을
수 없고 wrap하지 않는다. Duplicate record는 canonical bytes가 같을 때만 idempotent하다.

`OperationId`와 `ReplyRouteId`는 source owner lifecycle 안에서 각각 unique하고 wrap·reuse하지 않는다. Operation ID는
deduplication에 사용하고 reply routing에는 original `ReplyRouteId`를 사용한다. Durable terminal identity는 stable
`TransferId`와 `OperationId`다.

Target은 handler 결과와 delivery state를 새 immutable checkpoint에 기록하고 authority CAS로 root, terminal count와
pending relay count를 함께 바꾼다. Source의 authenticated `replyRelayAck` 또는 exact request-source owner lease
expiry가 확인되기 전에는 pending relay를 완료하지 않는다. Physical disconnect는 terminal delivery 증거가 아니다.

External side effect 뒤 checkpoint CAS 전에 process가 종료되면 handler가 같은 operation ID로 다시 실행될 수 있다.
Application은 operation ID 또는 자신의 durable key로 side effect를 idempotent하게 처리해야 한다.

## 10. Cleanup과 recovery

Durable source cleanup은 source callback·scope, old Entry membership, participant와 session state를 정리한 결과를
authority에 기록한다. `transferComplete`는 이 state가 terminal임을 확인한 뒤 target finalization을 한 번 알린다.
Current source lease가 끝났으면 recovery coordinator가 exact immutable source token을 확인하고
`SourceLeaseExpired`를 기록할 수 있다.

Host deadline에 도달하면 local resource를 bounded teardown하지만 committed transfer를 source로 rollback하지
않는다. Current authority와 checkpoint가 남아 있으면 다른 coordinator가 recovery를 이어간다. Cleanup과
reservation release는 반복 실행해도 같은 terminal state로 수렴해야 한다.

## 11. 검증

- Barrier inventory, create·join과 신규 admission이 한 순서로 정렬된다.
- Factory 실패와 ambiguous delete가 새 owner를 잘못 Ready로 publish하지 않는다.
- Connection-bound work가 durable journal에 포함되지 않는다.
- `Captured` CAS 전 crash에서 checkpoint replay를 시작하지 않는다.
- Target replacement가 stable TransferId와 checkpoint root를 바꾸지 않는다.
- Actor owner와 target Entry Spot membership이 같은 commit에서 바뀐다.
- `Activated`부터 route ACK·steady normalization 전까지 target admission이 닫혀 있다.
- Request terminal completion과 relay ACK가 같은 durable identity로 한 번만 수렴한다.
- Preflight와 reversible drain failure는 admission을 복원하고, `Draining` 뒤 실패는 `ForceStopped`로 끝난다.
