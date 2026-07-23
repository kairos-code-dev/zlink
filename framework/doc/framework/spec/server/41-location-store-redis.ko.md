# Redis Location Store — 공통 스펙

[스펙 목차](../README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Redis Relocation Store](42-relocation-store-redis.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위

이 문서는 Framework 11.0 Redis Location Store가 descriptor, host owner lease, durable object authority,
placement reservation과 aggregate commit을 저장하는 규칙을 정의한다. Provider는 object lifecycle, authority
payload와 relocation phase를 해석하지 않는다. Framework가 schema에 따라 bytes를 encode·decode하고 Redis는 key,
generation, StoreVersion과 atomic CAS만 관리한다.

Redis server time이 lease 만료의 기준이다. Application host의 wall clock은 authority 판단에 사용하지 않는다.

## 2. 저장 영역과 수명

Redis key prefix는 배포 단위에서 설정할 수 있지만 같은 provider transaction domain에서는 다음 논리 영역을
분리한다.

| 영역 | 값 | 수명 |
|---|---|---|
| Descriptor | MeshNode, ClientServer server, fanout publisher descriptor와 host owner lease token | host lease에 종속된 ephemeral data |
| Host owner lease | `(OwnerId, LeaseGeneration, ExpiresAt, StoreNow)` | Redis TTL |
| Object authority | canonical authority key, opaque payload, StoreVersion, ObjectGeneration, AuthorityOwnerGeneration | 명시적 fenced delete까지 durable, TTL 금지 |
| Authority index | snapshot scan용 versioned key index와 active scan lease | authority row와 scan lease에 종속 |
| Placement reservation | object key, stable type, target descriptor, capacity delta와 exact authority fence | Creating authority와 target owner lease로 recovery할 때까지 durable |
| Global counters | ObjectGeneration, AuthorityOwnerGeneration, StoreRevision, LeaseGeneration | provider transaction domain의 durable counter |

Authority row에 lease TTL을 설정하거나 host owner lease 만료 시 자동 삭제하지 않는다. Owner가 만료된 authority는
recovery coordinator가 exact StoreVersion CAS로 새 owner를 정하거나 명시적으로 삭제한다. 따라서 Redis key
expiry event를 authority transition으로 해석하지 않는다.

Per-key generation counter, per-key StoreVersion tombstone와 delete 뒤 유지하는 generation key는 만들지 않는다.
ObjectGeneration, AuthorityOwnerGeneration과 새 StoreVersion을 위한 StoreRevision은 provider transaction domain의
global durable counter에서 발급한다. Maximum은 `2^63-1`이다. 필요한 increment가 maximum에서 발생하면 closed
write result `GenerationExhausted`를 반환한다. 이 결과는 non-retriable이고 row, index와 모든 counter의 mutation·
consumption은 0이며 transport exception과 구분한다.

## 3. Host owner lease

한 Framework host process lifecycle은 owner lease token 하나를 claim한다. 같은 host의 모든 descriptor, placement
reservation과 object authority가 이 token을 공유한다. Token은 Framework가 만든 재사용 불가능한 OwnerId와
provider가 global counter에서 발급한 non-zero LeaseGeneration의 조합이다.

Provider는 다음 exact operation만 제공한다.

- Claim과 expired-row Takeover는 OwnerId와 TTL을 받아 새 token, ExpiresAt과 StoreNow를 반환하거나
  conflict 또는 `GenerationExhausted`로 끝난다. `GenerationExhausted`는 새 LeaseGeneration이 필요한
  경우에만 가능하며 row, index와 counter를 변경하거나 소비하지 않는다.
- Read는 OwnerId를 exact key로 읽어 current token, ExpiresAt과 StoreNow를 반환하거나 Missing으로 끝난다.
- Renew는 exact token과 TTL을 비교해 같은 token의 expiry만 갱신하거나 Stale로 끝난다.
- Release는 exact token을 비교해 lease와 ephemeral owner index를 제거하거나 Stale로 끝난다.

Renew와 Release는 새 LeaseGeneration을 발급하지 않으므로 `GenerationExhausted`를 반환하지 않는다.

Provider-wide `ListOwnerLeases`는 제공하지 않는다. Routing, resolve와 relocation admission은 항상 필요한 OwnerId를
exact Read한다. Lease 목록은 권한 판단 근거가 아니다.

`RemoveAllByOwner`는 `(OwnerId, LeaseGeneration)` exact token으로 index된 ephemeral descriptor만 제거한다.
TTL-free object authority와 placement reservation은 제거하지 않는다. Authority와 reservation은 exact fence를
사용하는 explicit recovery operation만 변경한다.

## 4. Durable authority CAS

### 4.1 Canonical key와 read

Framework는 `service-wire-v1.schema.json`의 `authority-key-v1` 규칙으로 canonical key를 만든다. Redis provider는
key bytes를 opaque 값으로 취급한다. Direct resolve는 exact key read만 사용한다.

Actor key는 global ActorId 하나, Spot key는 global SpotRid 하나로 구성한다. MeshName을 key prefix나 uniqueness
scope에 넣지 않는다. MeshName은 opaque authority payload의 placement attribute이며 Redis provider는 이를
해석하지 않는다.

Read 결과는 다음 closed union이다.

- `Missing(StoreNow)`: row가 없으며 StoreVersion이나 generation을 만들지 않는다.
- `Found(Payload, StoreVersion, ObjectGeneration, AuthorityOwnerGeneration, OwnerId,
  OwnerLeaseGeneration, StoreNow)`: current row snapshot을 반환한다.

Missing 결과에 `0`, 빈 문자열이나 synthetic StoreVersion을 넣지 않는다.

### 4.2 Expectation과 mutation

모든 authority mutation은 `Missing` 또는 `Found(expected StoreVersion)` expectation을 명시한다. Redis Lua/function은
expectation을 먼저 검증한 뒤 global counter 소비, row write와 index write를 한 atomic operation으로 처리한다.

| Transition | 허용 expectation | generation 결과 |
|---|---|---|
| Preserve | Found | ObjectGeneration과 AuthorityOwnerGeneration 유지, 새 StoreRevision만 발급 |
| NewOwner | Found | ObjectGeneration 유지, 새 AuthorityOwnerGeneration과 StoreRevision 발급 |
| NewObject | Missing | 새 non-zero ObjectGeneration, AuthorityOwnerGeneration과 StoreRevision 발급 |
| Delete | Found | 새 StoreRevision 발급 뒤 row와 current index entry 제거 |

Authority payload에는 provider generation과 StoreVersion을 중복 encode하지 않는다. Wire fence는 provider metadata와
opaque StoreVersion을 Framework가 조합한다. Redis script는 payload body를 parse하거나 수정하지 않는다.

Framework가 encode하는 authority payload는 current owner와 location, relocation phase, `RelocationId`, immutable
source와 current target fence, Relocation Store root reference와 checksum, membership, replay·completion count를
포함한다. Redis provider는 이 field를 해석하지 않지만 expected StoreVersion CAS는 reference, checksum, phase,
owner, membership과 count를 하나의 authority revision으로 바꾼다. 일부 field만 별도 operation으로 갱신하지 않는다.

Logical create는 아래 generic placement reservation operation을 사용한다. 단일-key `NewObject` CAS와 별도 capacity
mutation을 조합해 create를 구현하지 않는다.

### 4.3 Generic placement reservation

Provider는 object kind를 해석하지 않는 `Reserve`, `Commit`, `Abort` closed operation을 제공한다. Reservation은
object kind, global canonical key, stable type, immutable creation intent reference·hash, target descriptor key,
active·pending capacity delta와 exact owner fence를 가진다. TTL을 두지 않으며 Creating authority와 target host owner
lease를 기준으로 recovery, takeover 또는 abort한다.

| Operation | Atomic mutation |
|---|---|
| `Reserve` | `Missing → Creating`, generation 발급, creation intent 연결과 target pending capacity 증가 |
| `Commit` | exact `Creating → Ready`, pending 감소와 active 증가 |
| `Abort` | exact Creating authority 삭제와 pending 감소 |

Expectation, global counter, authority row·index, reservation과 capacity counter는 같은 server-side transaction에서
검증하고 변경한다. `Reserve`는 node active·pending 기본 limit과 type별 effective limit을 모두 확인한다. Limit을
넘으면 아무 값을 소비하지 않고 `PlacementCapacityExhausted`를 반환한다. `Commit`과 `Abort`는 같은 reservation
fence에 대해 idempotent하며 stale reservation이 새 authority나 capacity를 변경하지 못한다.

Creation request는 encoded 최대 1 MiB이고 immutable content reference와 hash로 저장한다. Ready 또는 fenced abort가
확정될 때까지 reference를 유지한다. Provider는 content를 해석하지 않는다. Recovery는 owner lease가 stale한 pending
reservation을 exact fence로 takeover하거나 abort하며, elapsed time만으로 reservation을 삭제하지 않는다.

### 4.4 Bounded aggregate commit

User Spot relocation과 cross-node Actor `JoinSpot`·`JoinEntrySpot`은 generic bounded aggregate transaction을 사용한다. Request는 non-zero
128-bit aggregate ID, exact aggregate generation, participant별 authority key·expected StoreVersion·owner mutation과
membership mutation을 가진다. Participant는 최대 1024개이고 encoded request와 aggregate record는 각각 최대
1 MiB다. Provider는 User Spot이나 Actor 의미를 해석하지 않고 expectation과 mutation vector만 처리한다.

Location Store의 aggregate record가 bounded canonical participant set, participant별 mutation, aggregate generation과
inventory digest를 권한 원본으로 저장한다. Relocation Store manifest는 participant별 state·journal payload를 찾기
위한 같은 digest의 projection일 뿐 authority가 아니다. Location Store transaction은 Relocation Store를 호출하거나
두 Store 사이 2PC를 수행하지 않는다.

`PrepareAggregate`는 모든 participant expectation, target reservation과 owner lease fence를 확인하고 durable
prepared record를 만든다. `CommitAggregate`는 prepared record의 exact generation에서 모든 authority owner,
AuthorityOwnerGeneration, membership index와 aggregate commit generation을 한 server-side transaction으로
전환한다. `AbortAggregate`는 commit 전 prepared record와 reservation만 정리한다. 같은 aggregate generation의
duplicate operation은 idempotent하고 다른 generation은 stale다.

Expectation 하나라도 맞지 않으면 participant row, membership index, reservation, aggregate record와 counter를
변경하지 않는다. Commit 전에는 target owner나 membership 일부를 authority read와 index scan에 공개하지 않는다.
Commit 뒤에는 source participant 일부로 rollback하지 않으며 exact aggregate record를 사용해 target 전체 recovery를
계속한다. Recovery는 aggregate ID, generation, participant fence와 current owner lease를 모두 확인한다.

## 5. Snapshot-consistent authority scan

Authority recovery scan은 page size 1..1000과 encoded page 최대 4 MiB를 함께 지킨다. 한 row가 authority envelope
1 MiB를 넘으면 저장 단계에서 거부되므로 page provider가 row를 잘라 반환하지 않는다.

첫 call은 cursor를 전달하지 않는다. Provider는 snapshot watermark와 scan lease를 내부에 만들고 item과 optional
`AuthorityScanCursor`를 반환한다. Cursor는 non-empty opaque bytes이며 encoded 크기는 4096 bytes 이하이다.
Framework는 cursor를 해석, 조합하거나 다른 scan에 섞지 않는다. Expired, replayed 또는 다른 scan의 cursor는
`ScanExpired` closed result로 끝난다.

한 scan은 첫 page watermark에 존재한 row incarnation을 canonical key byte 순서로 정확히 한 번 반환한다.
Concurrent delete row는 candidate exact read에서 Missing일 수 있고 watermark 뒤 create·recreate는 다음 scan에서
관찰한다. Provider는 active scan lease가 참조할 수 있는 versioned index entry와 delete tombstone만 유지하고 모든
older scan lease가 끝나거나 만료되면 GC한다. 이 tombstone은 per-key generation 또는 routing authority가 아니다.

Redis `SCAN`, 모든 row를 한 번에 복사하는 Lua와 unbounded materialization은 금지한다. Startup은 exact host owner
token과 recovery partition에 속한 complete snapshot scan을 끝낸 뒤 stateful serving을 연다. Background recovery도
같은 page 계약을 사용한다.

## 6. Descriptor enumeration

Descriptor list는 page size 1..1000과 encoded page 최대 4 MiB를 지킨다. Framework는 scope change stamp를 page
열거 전후에 읽고 두 값이 같을 때만 전체 page 결과를 desired set으로 적용한다. 값이 다르면 결과를 버리고
bounded retry한다. Continuation은 provider-issued opaque cursor이며 provider가 unbounded list를 먼저 만들면 안 된다.

Host는 startup에서 모든 Channel, type capability와 Snapshot adapter capability를 포함한 complete descriptor를
먼저 만든다. Encoded descriptor는 최대 1 MiB이고 type·Snapshot capability vector는 각각 최대 1024개다.
하나라도 넘으면 configuration/startup을 atomic하게 실패한다. Descriptor를 truncate, split하거나
일부만 publish하지 않는다. Descriptor는 application state format·version을 싣지 않으며 그 호환성은 target
adapter의 `Restore` 결과로 판단한다.

`update`는 current admitted physical connection에만 적용한다. Topology, identity, endpoint connection identity,
RID, lifecycle generation, normalized max message size, channel membership key, capability와 application version은
immutable하다. Existing channel weight, runtime state, capacity와 maintenance wave만 더 큰 revision으로 갱신한다.
Runtime state의 `Retiring`은 새 selection·placement·membership과 inbound relocation target을 닫지만 아직 seal하지
않은 current owner route를 유지하는 Framework 의미다. Redis provider는 이 값을 재해석하지 않고 descriptor
snapshot과 revision 규칙에 따라 저장한다.
같은 revision·같은 bytes는 idempotent이고 lower revision은 stale다. 같은 revision의 다른 bytes나 immutable 변경은
protocol error이며 connection을 not-ready로 바꾼다.

DescriptorRevision은 Framework caller가 descriptor마다 발급하며 Redis provider counter가 아니다. 값이
`2^63-1`에 도달해 다음 revision이 필요하면 Framework host는 wrap하지 않고 `Error`로 seal하며
publish를 시도하지 않는다.

MeshNode descriptor의 object role, stable type capability와 capacity limit은 immutable하다. Node-wide placement
weight와 current active·pending count는 더 큰 revision으로 갱신할 수 있다. Channel weight를 placement weight로
합성하지 않는다. Reservation transaction이 변경한 count와 descriptor projection이 같은 값을 나타내야 하며,
projection lag를 capacity 권한으로 사용하지 않는다.

## 7. Routing ID descriptor owner CAS

Automatic RID의 uniqueness는 descriptor owner CAS가 `(MeshName, RID)` active conflict를 atomic하게 확인해
보장한다. RID는 Framework가 `prefix-<32 lowercase hex>` 형식으로 생성하며 Redis provider는 RID 구조나 prefix를
해석하지 않는다. Claim은 exact host owner lease token과 descriptor identity를 함께 기록하고, renew·update·release는
같은 token을 비교한다.

Active conflict는 기존 descriptor를 덮어쓰지 않는다. Framework는 새 random RID로 최대 8회 claim을 시도하고 계속
충돌하면 `RoutingIdConflict`로 startup을 실패한다. Replacement lifecycle은 새 RID로 새 claim을 수행한다.

## 8. Store 장애와 recovery

`StoreFailureGrace`는 descriptor discovery reconcile과 새 outbound connect에만 적용한다. 마지막 stable desired set은
grace 동안 유지할 수 있고 existing transport는 service liveness를 계속 적용한다. Grace가 끝난 뒤 stable page
snapshot을 다시 얻기 전에는 새 connection을 만들지 않는다.

Grace는 host owner lease, coordinator lease와 local authority deadline을 연장하지 않는다. 마지막 valid owner lease
read에서 계산한 monotonic deadline에 도달하면 Actor·Spot·Instance message, timer, factory completion, relocation
source·target·coordinator CAS와 reservation admission을 seal한다. Store 복구 뒤 exact owner token과 stable page set을
다시 검증한 다음 diff와 new connect를 적용한다.

Recovery는 authority scan item을 exact Read한 뒤 expected StoreVersion CAS로만 변경한다. Scan item payload,
descriptor snapshot 또는 expired owner ID만으로 owner를 바꾸지 않는다.

## 9. Atomicity와 오류

Redis implementation은 row, global counter, index와 Redis `TIME`을 읽고 쓰는 operation을 하나의 Lua script 또는
동등한 server-side atomic function으로 구현한다. Cluster deployment는 한 transaction domain의 관련 key가 같은
hash slot에 배치되도록 prefix/hash tag를 구성한다. 이를 보장할 수 없으면 provider startup을 실패한다.

Script timeout, failover와 connection loss로 commit 여부가 불명확하면 Framework는 exact Read로 결과를 확인한다.
같은 expectation을 임의로 새 mutation처럼 재실행하지 않는다. Opaque payload decode failure, counter overflow와
authority의 relocation reference·checksum·replay count 불일치는 recovery error이며 Ready/Completed를 publish하지
않는다. Published authority가 가리키는 Relocation root가 영구적으로 없으면 non-retriable `RelocationDataLost`로
seal하고 이전 source owner로 rollback하지 않는다.

Provider operation을 시작하기 전 cancellation은 I/O와 commit을 모두 막을 수 있다. Operation을 시작한 뒤 waiter
cancellation, timeout 또는 provider error는 commit 실패를 뜻하지 않으며 결과가 불명확하다. Authority CAS는 exact
key와 expected fence를 다시 읽어 reconcile한 뒤에만 retry한다. Relocation Store의 Put·renew·delete retry와 orphan
처리는 [Redis Relocation Store](42-relocation-store-redis.ko.md)가 소유한다.

Framework가 provider에 넘긴 key와 value bytes는 async operation 완료까지 변경되지 않고 유효해야 한다. Provider가
그 이후 bytes를 보관하려면 복사한다. Provider success result의 bytes는 immutable하고 호출자가 보관할 수 있어야
한다. Mutable Redis adapter buffer를 사용하면 provider가 반환 전에 defensive snapshot을 만든다.

## 10. 검증 요구

- Missing read가 StoreNow만 반환하고 synthetic StoreVersion과 generation을 만들지 않는다.
- ActorId와 SpotRid가 MeshName과 독립적인 global authority key로 저장된다.
- NewObject와 NewOwner가 global counter와 row/index를 한 operation으로 변경한다.
- 모든 generation counter가 `2^63-1`에서 `GenerationExhausted`를 stable하게 반환하고 아무 값도 소비하지 않는다.
- Authority row에 TTL이 없고 host owner lease 만료만으로 row가 삭제되지 않는다.
- `RemoveAllByOwner`가 exact host token의 ephemeral descriptor만 제거하고 durable authority·reservation은 유지한다.
- Authority scan이 1000 item·4 MiB·4096-byte opaque cursor와 snapshot consistency를 지킨다.
- Descriptor page가 1000 item·4 MiB를 지키고 unstable scope stamp 결과를 적용하지 않는다.
- Oversize descriptor와 capability vector가 startup을 실패시키며 partial descriptor를 publish하지 않는다.
- Descriptor owner CAS가 active RID conflict를 덮어쓰지 않으며 exact host lease fence를 사용한다.
- `Reserve`, `Commit`, `Abort`가 authority와 node·type capacity를 atomic하게 전환한다.
- Pending reservation recovery가 elapsed time이 아니라 exact owner lease와 authority fence를 사용한다.
- Creation request reference와 hash가 Ready 또는 fenced abort까지 유지되고 encoded 1 MiB bound를 지킨다.
- Bounded aggregate transaction이 최대 1024 participant·1 MiB record에서 owner와 membership을 한 commit
  generation으로 전환하고 partial visibility를 만들지 않는다.
- Relocation reference·checksum, owner·membership, phase와 replay·completion count가 한 authority CAS로 바뀐다.
- Bounded canonical participant set, participant별 mutation, aggregate generation과 inventory digest가 같은 Location
  transaction에서 commit된다.
- Published relocation root가 영구적으로 없으면 `RelocationDataLost`로 seal하고 source로 rollback하지 않는다.
- StoreFailureGrace가 discovery만 freeze하고 authority deadline을 연장하지 않는다.
- Redis failover 뒤 exact read가 uncertain CAS의 실제 결과를 결정한다.
- Commit 성공 뒤 response loss·waiter cancellation이 rollback으로 오인되지 않고 exact read로 reconcile된다.
- Async provider operation 동안 input bytes가 유지되고 result bytes가 immutable snapshot이다.
