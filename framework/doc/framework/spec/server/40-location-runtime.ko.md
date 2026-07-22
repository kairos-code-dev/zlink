# Location Runtime — 공통 스펙

[스펙 목차](../README.ko.md) · [Spot address messaging](24-spot-address-messaging.ko.md) ·
[Redis Location Store](41-location-store-redis.ko.md) · [Redis Transfer Store](42-transfer-store-redis.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위와 책임

Location runtime은 Framework service runtime의 discovery, global Actor·Spot authority, placement reservation,
logical create, route cache와 stateful maintenance를 조정한다. Core raw transport는 Location key, owner lease,
Actor·Spot identity, creation intent, transfer envelope과 transfer phase를 해석하지 않는다.

Location Store와 Transfer Store는 별도 registration capability다. Location Store는 owner·location·generation,
transfer phase와 `TransferId`, source·target fence, Transfer root reference·checksum, canonical participant set,
membership, placement reservation, aggregate generation·commit과 replay·completion count를 expected-version CAS로
원자적으로 변경한다. Transfer Store는 application state, accepted journal, participant별 state·journal과
replay·recovery payload를 immutable root로 보관한다. Actor·Spot별 Store interface는 제공하지 않는다.

Object Server factory에 `Recreate` 또는 `Snapshot` policy를 하나라도 등록한 Framework root는 Transfer Store를
정확히 하나 등록해야 한다. 누락되거나 둘 이상이면 socket bind 전에 startup configuration error다. 모든 factory가
`Disabled`이면 Transfer Store가 필요하지 않고 cross-node 이동은 capture 전에 거부한다. Same-node Actor join은
Location Store membership transaction만 사용하며 Transfer payload를 만들지 않는다.

| 이동 조건 | Transfer Store 사용 |
|---|---|
| Same-node Actor join | 사용하지 않는다. Location Store에서 membership만 commit한다. |
| `Disabled` cross-node 이동 | 사용하지 않는다. Capture 전에 거부한다. |
| `Recreate` cross-node Actor·Spot 이동 | Application state 없이 accepted journal과 recovery payload를 저장한다. |
| `Snapshot` cross-node Actor·Spot 이동 | Application state, accepted journal과 recovery payload를 저장한다. |
| Host maintenance의 Actor·User Spot aggregate | Participant별 state·journal payload와 replay manifest를 저장한다. |
| Cross-node Actor `JoinSpot`·`JoinEntrySpot` | 이동하는 Actor의 policy에 맞는 payload를 저장한다. |

Location Store는 다음 두 종류의 정보를 분리한다.

- Descriptor는 host owner lease에 종속된 ephemeral discovery data다.
- Actor·Spot authority는 명시적 fenced delete까지 유지되는 durable data다.

Transport ready, descriptor 존재와 owner authority는 서로 다른 조건이다. Resolver와 placement는 필요한 조건을
모두 확인하며 어느 한 신호로 다른 조건을 대신하지 않는다.

Object role이 `Client` 또는 `Server`인 MeshNode는 Location Store가 필수다. Store가 없으면 startup을 실패하며
runtime-local manager, hidden authority나 local CAS provider를 만들지 않는다. Role이 `None`인 MeshNode는 object
create, find, message와 factory를 제공하지 않는다.

## 2. Identity와 generation

### 2.1 Host lifecycle token

한 Framework host process lifecycle은 `(OwnerId, LeaseGeneration)` token 하나를 사용한다. OwnerId는 Framework가
만드는 재사용 불가능한 값이고 LeaseGeneration은 Store provider가 global durable counter에서 발급하는 non-zero
값이다. 같은 host의 모든 MeshNode·ClientServer·fanout descriptor, automatic RID claim, Actor·Spot authority와
maintenance role이 이 token을 공유한다.

LeaseGeneration은 host lifecycle fence다. Object owner가 바뀌는 횟수를 나타내지 않는다. Provider는 필요한
OwnerId를 exact read하며 provider-wide owner lease list를 권한 판단에 사용하지 않는다.

Owner lease Claim과 expired-row Takeover가 새 LeaseGeneration을 발급해야 하는데 global counter가 `2^63-1`이면
provider는 closed `GenerationExhausted`를 반환한다. 이 결과는 non-retriable이며 lease row, index와 counter를
변경하거나 소비하지 않는다. Renew와 Release는 새 generation을 발급하지 않으므로 이 결과를 반환하지 않는다.

### 2.2 Object authority generation

Durable authority metadata는 다음 값을 분리한다.

| 값 | 의미 |
|---|---|
| ObjectGeneration | 같은 canonical object key가 delete 뒤 새 object로 생성된 incarnation |
| AuthorityOwnerGeneration | 같은 object incarnation의 authority owner가 바뀐 순서 |
| StoreVersion | exact CAS expectation에 사용하는 opaque row revision |
| OwnerLeaseGeneration | current host process lifecycle token의 generation |

이 값은 모두 provider가 발급한다. ActorRef와 SpotRef의 generation은 ObjectGeneration이다.

ObjectGeneration, AuthorityOwnerGeneration과 새 StoreVersion을 위한 StoreRevision은 provider transaction domain의
global monotonic counter를 사용하고 maximum은 `2^63-1`이다. 필요한 increment 시 counter가 maximum이면 provider는
closed write result `GenerationExhausted`를 반환한다. 이 결과는 non-retriable이며 row, index와 모든 counter의 mutation·
consumption은 0이다. 같은 상태의 반복 호출도 같은 결과다. Transport exception과 혼합하지 않는다. Runtime은 해당
authority를 error로 seal하고 wire command를 보내지 않으며 host snapshot과 error event에 원인을 기록한다. Counter를
reset하거나 wrap해 복구하지 않는다.

### 2.3 Authority snapshot

Authority canonical key는 Actor의 global ActorId 또는 Spot의 global SpotRid다. 두 ID는 UTF-8 1..255 bytes,
case-sensitive exact value이며 normalization과 case folding을 하지 않는다. Spot row는 Entry, User와 Instance
kind를 closed union으로 구분한다. MeshName은 identity key가 아니라 current placement attribute다. Snapshot은
opaque payload와 다음 provider metadata를 포함한다.

- StoreVersion, ObjectGeneration과 AuthorityOwnerGeneration
- current OwnerId와 OwnerLeaseGeneration
- owner node RID와 lifecycle generation
- object kind, stable type, immutable identity, initial·current MeshName과 current state
- logical create 중이면 immutable creation intent reference, content hash와 placement reservation fence
- maintenance 중이면 compact transfer state

Authority row에는 TTL이 없다. Owner lease가 만료돼도 row는 남으며 recovery coordinator가 expected StoreVersion
CAS로 owner를 교체하거나 명시적으로 삭제한다. Missing read는 StoreNow만 반환하고 synthetic StoreVersion이나
generation을 만들지 않는다.

## 3. Descriptor와 discovery

MeshNode, ClientServer server와 fanout publisher descriptor는 current host owner lease token을 포함한다. Automatic
consumer는 descriptor page를 읽고 owner lease를 exact 확인한 뒤 desired connection을 계산한다.

DescriptorRevision은 Framework host가 descriptor마다 발급하는 non-zero monotonic 값이며 provider global counter가
아니다. 값이 `2^63-1`에 도달해 다음 revision이 필요하면 wrap하지 않고 host를 `Error`로 seal하며 새
descriptor를 게시하지 않는다.

Descriptor page는 request item 1..1000개, encoded 최대 4 MiB다. Framework는 scope change stamp를 열거 전후에
읽고 값이 같을 때만 complete page set을 적용한다. Continuation은 provider-issued opaque cursor다. Provider가
전체 list를 Lua에서 materialize하거나 Redis `SCAN` 결과를 그대로 공개하면 안 된다.

Host는 startup에서 complete descriptor를 먼저 만든다. Encoded descriptor 최대 크기는 1 MiB다. Type/stateful
capability vector와 type별 readable state-contract set은 각각 최대 1024개다. 초과하면 startup/configuration을
atomic하게 실패하고 descriptor를 truncate, split하거나 일부 publish하지 않는다.

Descriptor가 connection intent를 만들 수는 있지만 ready를 증명하지 않는다. RouteMesh와 ClientServer는 current
physical connection의 service admission을 통과해야 한다. Fanout subscriber는 publisher별 전용 SUB socket에서 첫
valid application record 또는 exact beacon을 받은 뒤 ready다.

Object Server descriptor는 `Server` role, node-wide placement weight, node active·pending capacity와 type별
capability를 포함한다. Weight는 0..100이고 기본값은 100이다. Node capacity 기본값은 active 10,000, pending
128이다. Type별 limit은 생략하면 node limit을 공유하며 명시하면 1..`2^31-1` 범위에서 더 작은 값을 적용한다.
Placement는 current lease와 `Serving` 상태를 확인하고 active·pending capacity를 먼저 적용한 뒤 positive weight
비율로 target을 고른다. Weight 0은 새 reservation과 transfer target에서만 제외하며 Ready object와 이미 완료된
reservation은 취소하지 않는다.

## 4. Owner lease와 local admission deadline

Location Store를 사용하는 모든 host는 startup에서 다음 관계를 검증한다. Routing ID allocation 사용 여부와
무관하다.

```text
renew interval + renew timeout < owner lease TTL - owner lease fencing margin
```

공통 기본값은 renew interval 5초, TTL 15초, renew timeout 3초, owner lease fencing margin 5초다. 값은 모두
0보다 커야 하고 위 관계가 성립하지 않으면 startup을 실패한다. Automatic RID descriptor claim도 별도 deadline을
만들지 않고 같은 host token과 deadline을 사용한다.

성공한 Claim·Read·Renew 결과의 StoreNow와 ExpiresAt으로 남은 시간을 계산하고 operation 시작 전후 local monotonic
시각을 읽어 보수적인 deadline을 만든다. 한 번 성공한 host-token renew가 host 전체의 shared local admission
deadline을 갱신한다. Object별 deadline이 이 값을 연장하지 않는다.

Deadline을 넘거나 exact token read가 stale이면 다음 admission을 즉시 seal한다.

- descriptor publication과 automatic RID owner mutation
- Actor·Spot·Instance message와 timer turn
- factory·restore completion commit
- transfer source·target·coordinator phase CAS와 target reservation

이미 local queue에 accept한 turn의 terminal 처리와 cleanup은 별도 deadline 계약에 따라 진행할 수 있지만 stale
owner 권한으로 새 mutation을 만들지 않는다.

## 5. Authority operation

### 5.1 Read와 CAS

Authority Read는 `Missing(StoreNow)` 또는 `Found(snapshot, StoreNow)` closed result다. Mutation expectation은
`Missing` 또는 `Found(expected StoreVersion)`다.

- Preserve는 object와 authority owner generation을 유지하고 StoreRevision만 바꾼다.
- NewOwner는 ObjectGeneration을 유지하고 새 AuthorityOwnerGeneration을 발급한다.
- NewObject는 Missing에서만 새 ObjectGeneration과 AuthorityOwnerGeneration을 함께 발급한다.
- Delete는 Found에서만 row와 current index entry를 제거한다.

Provider는 expectation 검증, global counter 소비, row와 index 변경을 atomic하게 수행한다. Opaque payload 안에
provider metadata를 중복 encode하지 않는다.

### 5.2 Snapshot-consistent scan

Recovery scan은 1..1000 item과 encoded 4 MiB 상한을 지킨다. 첫 call은 cursor가 없고 provider가 snapshot
watermark와 scan lease를 내부에 만든다. `AuthorityScanCursor`는 non-empty, 최대 4096-byte opaque value다.
Framework는 이를 parse, 조합하거나 다른 scan에 섞지 않는다. Expired, replayed 또는 cross-scan cursor는
`ScanExpired`로 끝난다.

첫 watermark에 존재한 row incarnation을 canonical key byte 순서로 정확히 한 번 반환한다. Scan item은 recovery
후보일 뿐이다. Framework는 exact key Read와 expected StoreVersion CAS를 다시 수행한다. Provider는 active scan
lease가 참조하는 versioned index와 tombstone만 보관하고 older scan이 모두 끝나면 GC한다.

## 6. Logical create와 placement reservation

Actor, User Spot과 Instance Spot은 Manager의 명시적인 `Create` 또는 `GetOrCreate`로 생성한다. ActorId와 SpotRid는
global canonical key이고 MeshName은 initial placement attribute다. Actor operation은 required ActorId를 받는다.
User·Instance Spot의 `Create`는 Framework가 SpotRid를 발급하고, `GetOrCreate`는 caller의 SpotRid와 stable type을
받는다. Entry Spot identity는 Framework만 발급한다.

Create option에 `InMesh`가 있으면 그 Mesh를 사용한다. 생략했고 Object Client 또는 Server role의 Mesh가 하나이면
그 Mesh를 사용한다. 후보가 0개면 `ObjectClientNotConfigured`, 둘 이상이면 `MeshSelectionRequired`, 명시한 Mesh가
없으면 `MeshNotFound`다.
Create call은 object kind에 따라 필요한 identity와 stable type, optional MeshName·creation request·
`PlacementProfile`·`AffinityKey`·timeout을 가진 single-use operation이다. Terminal submit 시점에 resolve,
reservation, factory와 Ready barrier 전체에 적용할 하나의 end-to-end deadline을 고정한다. 같은 option을 두 번
설정하면 `InvalidConfiguration`, terminal submit을 두 번 호출하면 `AlreadySubmitted`다.

Creation request는 encoded 최대 1 MiB다. Reservation 전에 immutable content reference와 hash를 durable creation
intent에 기록하고 Ready 또는 fenced failure cleanup까지 유지한다. Factory는 `(logical key, ObjectGeneration,
creation attempt)`에 대해 at-least-once 실행될 수 있으므로 retry-safe해야 한다.

Missing object의 생성은 다음 순서를 따른다.

1. Complete descriptor와 exact lease에서 role, type, profile, active·pending capacity를 만족하는 target을 찾고
   positive placement weight로 하나를 선택한다.
2. Generic `Reserve`가 `Missing → Creating` authority, creation intent와 target pending capacity를 하나의 atomic
   transaction으로 기록한다. ObjectGeneration과 AuthorityOwnerGeneration은 이때 발급한다.
3. Target은 exact authority, reservation, node lifecycle과 owner lease를 확인하고 factory와 initialization을
   실행한다.
4. Generic `Commit`이 같은 reservation에서 `Creating → Ready`와 pending-to-active capacity 전환을 atomic하게
   수행한다.
5. Runtime은 global ID, ObjectGeneration, current MeshName과 NodeRid가 포함된 current ref를 반환한다.

`Create`가 Ready object를 찾으면 같은 stable type이어도 `AlreadyExists`다. `GetOrCreate`는 같은 type의 Ready
object를 반환하고 같은 type의 Creating attempt이면 그 completion에 합류한다. 다른 Actor type 또는 Spot kind·type은
`TypeMismatch` 또는 `SpotTypeMismatch`다. CAS loser는 다른 target을 선택하거나 factory를 시작하지 않는다.
Creating waiter가 deadline에 도달하면 `DeadlineExceeded`로 끝나며 다음 call이 exact authority를 reconcile한다.

Factory 또는 Ready commit 실패는 같은 reservation의 generic `Abort`로 Creating authority와 pending capacity를 함께
정리한다. Owner lease가 stale이면 recovery coordinator가 exact fence로 attempt를 takeover하거나 abort한다.
Caller cancellation, timeout이나 response loss는 commit 실패를 의미하지 않으므로 exact read로 결과를 확인한다.
Original creation payload와 일반 message를 다른 owner에 hidden retry하지 않는다.

Ready owner가 사라진 Instance Spot을 다시 만들 때는 authority에 저장한 stable type, initial Mesh와 creation intent를
사용한다. 일반 message는 type, Mesh나 creation request를 제공하지 않으며 Missing object를 만들지 않는다.

### 6.1 Find, ref와 exact mutation

Manager `Find(global ID)`는 existing Ready authority만 반환하며 create를 시작하지 않는다. ActorRef와 SpotRef는
`{global ID, ObjectGeneration, MeshName, NodeRid}` location snapshot이다. ObjectGeneration은 non-zero unsigned
63-bit conceptual value이고 JSON에서는 decimal string이다. Ref의 MeshName과 NodeRid는 current location을
진단하거나 exact generation을 고정하는 snapshot이며 일반 message target이나 placement input이 아니다.

Destroy와 Close는 exact ref를 받는다. 같은 incarnation이 없으면 idempotent `false`, 다른 generation이면
stale-generation error, moving이면 typed moving error다. Runtime은 current ref를 다시 찾아 새 incarnation을 종료하지
않는다.

### 6.2 Route cache와 stale-route forwarding

Positive Ready cache entry는 global key, ObjectGeneration, AuthorityOwnerGeneration, StoreVersion, owner lease,
node lifecycle과 route를 보존한다. `RouteCacheMaxAge` 기본값은 15초이고 current owner lease의 local admission
deadline을 넘을 수 없다. Missing, Creating과 Store failure는 cache하지 않는다. Store recovery, higher
StoreVersion, stale result나 lease invalidation은 entry를 즉시 제거한다.

`TransferForwardingWindow` 기본값은 30초다. 두 duration은 0이면 각각 cache와 forwarding을 끈다. 둘 다 양수이면
cache max age가 forwarding window보다 최소 5초 작아야 한다. Runtime 변경은 새 cache entry와 새 transfer에만
적용한다. 범위나 두 값의 관계를 위반한 설정은 runtime에 적용하지 않고 configuration error로 끝낸다.

Relay는 committed source→target mapping만 사용하며 Store를 읽지 않는다. Mapping의 target
AuthorityOwnerGeneration은 source보다 커야 하고 chain은 최대 8 hops다. Mapping 하나의 대기열은 1024 message와
16 MiB를 넘지 않으며 negotiated message bound도 함께 지킨다. Original operation ID, ObjectGeneration, payload와
reply route를 보존한다. Loop, generation mismatch와 bound 초과는 typed stale-route error다.

## 7. Stateful maintenance authority

### 7.1 Stable identity와 compact authority

`TransferId`는 runtime이 CSPRNG로 만드는 non-zero 128-bit 값이며 transfer root, journal, replay, late completion과
terminal identity 전체에서 안정적이다. Active transfer와 retention 중 transfer root에서 collision을 확인하면
사용하지 않고 새 값을 만든다. Application에 노출하지 않는다.
`TargetAttemptGeneration`은 같은 transfer에서 target reservation을 교체하는 non-zero attempt fence일 뿐이다.
Target replacement만으로 immutable transfer root를 다시 만들거나 TransferId를 바꾸지 않는다.

Authority hot row는 encoded 최대 1 MiB이며 다음 compact 정보만 가진다.

- immutable source OwnerId·LeaseGeneration과 source node RID·generation
- current target attempt, target owner lease·node fence와 reservation
- coordinator owner lease·node fence
- phase, application version, transfer root reference와 checksum
- participant progress, TerminalCompletionCount, PendingRelayCount와 SourceCleanupState

Full journal, participant별 state·journal, reply payload와 terminal completion vector는 Transfer Store의 immutable
stream에만 저장한다. Canonical participant set, participant별 mutation, aggregate generation과 inventory digest는
Location Store가 authority로 저장한다. Transfer manifest의 같은 digest는 payload 탐색 projection이며 authority가 아니다.

### 7.2 Phase별 closed owner rule

| Phase | Main owner와 target rule |
|---|---|
| Preparing, Captured | main owner는 immutable source, target attempt·token·reservation 없음 |
| Prepared | main owner는 source, exact non-zero target attempt·owner lease·node·reservation과 transfer root 존재 |
| Committed..Completed | main owner는 exact current target, 같은 attempt·reservation·transfer root 존재 |
| Aborted | main owner는 source이며 abort ACK·cleanup·steady source normalization 전까지 admission sealed |

Prepared→Committed는 NewOwner CAS 한 번으로 수행한다. Source token은 terminal까지 바뀌지 않는다. Target
replacement는 target attempt, target owner lease·node와 reservation만 교체한다. Post-commit replacement는
Committed로 재진입하고 stale attempt는 completion commit과 application admission을 열 수 없다.

Source Entry Spot에 속한 standalone Actor `Retire`는 target Entry Spot identity를 exact reservation에 고정한다.
Committed CAS는 Actor owner, AuthorityOwnerGeneration과 current target Entry Spot membership을 한 번에 바꾸며
부분 상태를 공개하지 않는다.

User Spot과 그 member Actor는 하나의 maintenance aggregate다. Aggregate ID는 non-zero 128-bit이고 participant는
최대 1024개, encoded aggregate record는 최대 1 MiB다. Spot과 각 Actor의 policy·state contract를 함께 preflight하고,
하나라도 `Disabled`이거나 target capability를 충족하지 못하면 capture 전에 전체 aggregate를 차단한다. Generic
Store transaction은 Spot owner, 모든 Actor owner와 membership visibility를 같은 commit generation으로 전환한다.
Commit 전에는 partial target owner를 resolve하지 않고 commit 뒤에는 target aggregate 전체만 recovery한다.

Commit 뒤 target은 factory와 restore, target `OnJoined`, accepted journal replay 순서로 처리한다. Source는 그 뒤
`OnLeave`와 이전 Entry membership cleanup을 durable source cleanup으로 확정한다. Target은 이 cleanup, Completed,
route ACK와 steady normalization을 모두 끝내기 전에는 Ready route와 application admission을 공개하지 않는다.

### 7.3 Seal, Transfer root와 reservation

Preflight는 capability와 bounded headroom만 확인하며 final target reservation을 만들지 않는다. Source는 새 message와
timer admission을 reversible하게 seal하고 이미 accept한 turn을 완료한다. Connection-bound source에서 수락한 모든
work와 모든 bound-session request는 Captured 전에 terminal drain하고 journal에 넣지 않는다. Deadline 안에 끝나지
않으면 pre-Captured abort, `Blocked/TransferDisabled`와 admission 복원으로 끝낸다. Durable journal의 모든 frozen
record는 exact lease-backed source OwnerId·LeaseGeneration을 가진다.

그 뒤 exact participant boundary와 byte count를 계산한다. Preparing CAS 뒤 deterministic transfer logical stream을
immutable chunks와 root manifest로 쓰고
Captured CAS로 root를 연결한다. Exact inventory로 target offer·accept·reservation ACK를 끝낸 뒤 Prepared CAS를
수행한다. 모든 object가 Prepared가 된 뒤 host가 Draining을 publish한다.

Accepted journal의 crash replay 보장은 complete root가 Captured CAS로 authority에 연결된 이후에만 성립한다.
Preparing 또는 Transfer Store Put 중 source process가 종료되면 recovery는 transfer를 fenced abort하고 연결되지 않은
Put을 orphan cleanup 대상으로 둘다. 이 구간의 original request는 일반 connection failure, timeout 또는 cancellation
terminal 계약을 따르며 accepted work replay와 hidden remote activation을 보장하지 않는다.

Transfer logical stream은 최대 256 GiB, chunk data는 최대 64 MiB, root manifest는 최대 4096개 chunk를 가진다.
Retention은 24시간, renew threshold는 12시간이다. Staged component도 expiry를 추적하며 Captured와 Prepared CAS
직전에 complete tree의 remaining lifetime이 12시간보다 큰지 verify·renew한다. Missing 또는 partial renew는
precommit abort이고 root를 authority에 연결하지 않는다.

Application snapshot은 `framework-json-v1` typed contract를 사용한다. Property order와 insignificant whitespace는
의미가 없고 application bytes는 validation 뒤 original bytes 그대로 opaque하게 보관한다. 64-bit integer는 canonical
decimal string, 32-bit 이하는 JSON integer, enum은 case-sensitive name, bytes는 padded RFC4648 Base64다. Duplicate
property, missing required property, non-finite number와 암묵적 DateTime·decimal·UUID는 허용하지 않는다.

### 7.4 Activation, route barrier와 Ready

Target은 Transfer root restore와 journal replay 중 application admission을 sealed 상태로 유지한다. Target factory와
restore callback은 attempt 사이에 at-least-once로 실행될 수 있고 stale attempt와 겹칠 수 있다. Callback은 retry-safe
해야 하며 Framework는 external side effect의 exactly-once를 보장하지 않는다. Public callback에 TransferId를
노출하지 않는다. Only current exact owner와 TargetAttemptGeneration만 completion을 commit할 수 있다.

`Activated` 뒤에도 target을 Ready로 publish하지 않는다. Durable source cleanup state CAS, Completed authority CAS,
bound-session route commit과 routed ACK, maintenance authority의 steady normalization을 모두 마친 뒤 application
admission을 열고 Ready route를 publish한다. Resolver는 transfer payload가 남은 authority를 어느 phase에서도 Ready로
투영하지 않는다.

### 7.5 Late request completion과 acknowledgement

Accepted request의 frozen record는 OperationId, exact request-source OwnerId·LeaseGeneration·node RID·generation과
original non-zero ReplyRouteId를 함께 보관한다. OperationId는 dedupe identity이며 reply route를 대신하지 않는다.
Send/event record에는 ReplyRouteId가 없다.

OperationId와 ReplyRouteId는 source owner lifecycle 안에서 non-zero unique 값이다. Wrap하거나 같은 lifecycle에서
재사용하지 않으며 소진은 terminal runtime error다. Durable terminal identity는 TransferId와 OperationId의
조합이고 ReplyRouteId는 original correlation에만 사용한다.

Late completion은 immutable completion chunk와 새 root manifest를 먼저 만든 뒤 expected StoreVersion CAS 한 번으로
authority root, checksum, TerminalCompletionCount와 PendingRelayCount를 함께 교체한다. Count는 referenced transfer root에서
계산한다. Accepted request count와 terminal completion count가 같고 PendingRelayCount가 0일 때만 Completed가 가능하다.
불일치는 recovery error다.

Target은 maintenance transfer `replyRelay`를 original route로 재전송한다. Request source는 terminal result를 처음
받으면 `terminalReceived`, 이미 terminal이면 `alreadyTerminal` 상태의 `replyRelayAck`을 보낸다. ACK sender는 transfer
source가 아니라 frozen record의 exact request source다. Target은 authenticated connection 자체를 증거로 삼지 않고
frozen source fence와 OperationId를 검증한다.

Delivery state는 `Pending → TerminalReceived | AlreadyTerminal | SourceLeaseExpired`로만 전이한다. Physical connection
close와 reconnect는 terminal proof가 아니며 current route로 relay를 계속 시도한다. Source lease expiry는 accepted
record에 저장한 exact token을 Store가 Missing 또는 stale로 확인했을 때만 인정한다. Source lease가 유효한 채 Retire
deadline을 넘으면 ForceStopped로 끝내고 transfer root와 reply bytes를 24시간 recovery horizon 동안 유지한다.

### 7.6 Source cleanup과 abort

`transferComplete`는 durable SourceCleanupState가 `Completed`이거나 current coordinator가 immutable source token의
expiry를 exact 확인한 뒤 `SourceLeaseExpired`로 CAS한 경우에만 보낸다. Source sender면 authenticated source fence,
coordinator sender면 current coordinator fence를 검증한다. 같은 TransferId와 cleanup state의 duplicate는 idempotent다.

Precommit abort는 source admission을 즉시 열지 않는다. Source sealed 유지 → durable Aborted CAS → Aborted phase의
session abort route와 routed ACK → reservation·transfer orphan cleanup → steady source normalization CAS → source
admission reopen 순서다. Aborted 결정 전에 abort route를 보내거나 session을 unseal하지 않는다.

## 8. Store outage와 cancellation

`StoreFailureGrace`는 descriptor reconcile과 새 outbound connect만 마지막 stable desired set으로 freeze한다. Existing
transport는 service liveness를 계속 적용한다. Grace 이후 stable descriptor snapshot을 얻기 전에는 새 connect를
만들지 않는다.

Grace는 host owner lease와 coordinator deadline을 연장하지 않는다. Shared local admission deadline에서 stateful
message, timer, factory completion, transfer CAS와 reservation을 seal한다. Recovery는 exact owner token과 stable page
set을 재검증한 뒤 diff와 connection intent를 적용한다.

Provider operation 시작 전 cancellation은 invocation과 commit을 막을 수 있다. 시작 뒤 waiter cancellation,
timeout과 provider error는 commit 여부가 불명확하다. Authority CAS는 exact key와 expected fence를 다시 읽어
reconcile한 뒤 retry한다. Content-addressed Transfer Store Put은 verify하거나 idempotent retry하고 unlinked result는
orphan으로 정리한다. Input bytes는 async completion까지 immutable·live해야 하고 provider가 더 오래 보관하면
복사한다. Success result bytes는 stable immutable snapshot이다.

Transfer Store write와 root 검증이 성공한 뒤에만 Location Store CAS로 reference를 publish한다. Root replacement도
새 root를 먼저 저장하고 검증한 뒤 reference·checksum·count를 한 CAS로 바꾼다. Cleanup은 Location authority에서
reference를 release한 다음 Transfer Store delete를 수행한다. 두 Store는 distributed transaction이나 2PC를 요구하지
않으며 다른 Redis에 배치할 수 있다.

Published authority가 가리키는 root가 일시적으로 보이지 않으면 bounded retry와 exact authority re-read를 수행한다.
Permanent missing, checksum mismatch 또는 Location participant inventory digest와 Transfer manifest digest의 불일치는
non-retriable `TransferDataLost`다. Runtime은 authority를 error로 seal하고 commit된 owner·membership을 source로
rollback하거나 다른 root를 추측하지 않는다.

## 9. Cleanup

Host 종료는 새 descriptor, stateful message와 object creation admission을 먼저 seal한다. Accepted turn, maintenance,
late reply relay와 STREAM route barrier를 deadline까지 진행한 뒤 exact host token의 descriptor를
제거한다. `RemoveAllByOwner`는 durable authority를 제거하지 않는다. Authority는 explicit expected StoreVersion과
owner fence를 가진 delete만 허용한다.

Deadline을 넘으면 terminal ForceStopped result를 한 번 완료한다. Timer, Store callback, reconnect work와 observer가
runtime-owned resource보다 늦게 남지 않는다.

## 10. 검증 요구

- ObjectGeneration, AuthorityOwnerGeneration과 OwnerLeaseGeneration이 서로 다른 fence로 동작한다.
- Provider counter가 `2^63-1`이면 `GenerationExhausted`가 atomic no-write·no-consume로 반환되고 반복해도 같다.
- 모든 Location host가 automatic RID 사용 여부와 무관하게 owner lease timing relation을 startup에서 검증한다.
- Shared local owner lease deadline이 descriptor, object, timer와 transfer authority를 함께 seal한다.
- Descriptor overflow가 partial publish 없이 startup을 실패시킨다.
- Authority Read Missing이 synthetic StoreVersion을 만들지 않고 authority row에 TTL이 없다.
- Opaque 4096-byte scan cursor, 1000-item·4-MiB page와 snapshot consistency가 유지된다.
- Actor·Spot global key가 MeshName과 독립적으로 같은 authority row에 수렴한다.
- `Create`와 `GetOrCreate` 경쟁이 같은 Creating attempt에 수렴하고 CAS loser가 별도 factory를 시작하지 않는다.
- Generic reservation이 Creating authority와 pending capacity를 atomic하게 reserve·commit·abort한다.
- Creation request reference와 hash가 Ready 또는 fenced abort까지 유지되고 factory가 retry-safe하게 재개된다.
- 일반 message와 find가 Missing Instance Spot을 hidden create하지 않는다.
- Missing, Creating과 Store failure를 cache하지 않고 Ready cache가 owner admission deadline을 넘지 않는다.
- Forwarding chain이 8 hops, mapping별 1024 message·16 MiB bound와 generation 증가를 검증한다.
- Preflight가 final reservation을 만들지 않고 seal 뒤 exact inventory로 Prepared를 만든다.
- Preparing/Captured/Prepared/Committed..Completed의 main owner와 target field closed rule을 위반한 CAS가 실패한다.
- Target replacement가 stable TransferId와 transfer root를 유지하며 stale attempt의 commit과 admission을 막는다.
- Target은 Activated·Cleaning·Completed에서도 sealed이며 route ACK와 steady normalization 뒤에만 Ready다.
- Frozen request가 OperationId, exact source fence와 ReplyRouteId를 보존하고 send에는 reply route가 없다.
- Standalone Actor `Retire`가 source Entry Spot membership을 target Entry Spot membership과 atomic하게 전환한다.
- User Spot aggregate가 bounded participant record를 사용하고 Spot·Actor owner와 membership을 한 commit
  generation에서 전환한다.
- Completion root와 authority count가 atomic하게 일치하고 ACK 또는 exact source lease expiry 전에는 transfer root를
  release하지 않는다.
- Transfer root write·verify가 Location CAS보다 먼저이고 reference release가 root delete보다 먼저다.
- Published root의 permanent missing·checksum mismatch·inventory digest mismatch가 `TransferDataLost`로 seal되며
  source rollback을 시작하지 않는다.
- Physical connection close가 request terminal proof로 사용되지 않는다.
- Aborted CAS 전에 session abort route와 source reopen이 발생하지 않는다.
- StoreFailureGrace가 discovery만 freeze하고 authority deadline을 연장하지 않는다.
- Commit 성공·response loss·cancellation race를 exact read 또는 idempotent Put으로 reconcile한다.
