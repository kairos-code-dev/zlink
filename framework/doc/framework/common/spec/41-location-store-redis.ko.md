# Redis Location Store

[공통 스펙 목차](README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Redis Relocation Store](42-relocation-store-redis.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 이 문서가 정의하는 범위

이 문서는 Framework 11.0 Redis Location Store가 다음 정보를 저장하는 규칙을
정의한다.

- MeshNode descriptor, ClientServer Server descriptor와 fanout publisher descriptor를
  통칭하는 discovery descriptor
- Host owner lease
- Durable object authority
- Placement reservation
- Aggregate commit

Provider는 object lifecycle, authority payload와 relocation phase를 해석하지 않는다.
Framework가 schema에 맞춰 bytes를 encode하고 decode한다. Redis provider는 key,
generation, `StoreVersion`과 atomic CAS를 관리한다.

Redis server time이 lease 만료의 기준이다. Application host의 wall clock은
[authority](01-glossary.ko.md#authority) 판단에 사용하지 않는다.

## 2. 저장 영역과 수명

Redis key prefix는 배포 단위에서 설정할 수 있지만 같은 provider transaction domain에서는 다음 논리 영역을
분리한다.

| 영역 | 값 | 수명 |
|---|---|---|
| Discovery [descriptor](01-glossary.ko.md#descriptor) | [MeshNode descriptor](01-glossary.ko.md#meshnode-descriptor), [ClientServer Server descriptor](01-glossary.ko.md#clientserver-server-descriptor) 또는 [fanout publisher descriptor](01-glossary.ko.md#fanout-publisher-descriptor)와 host [owner lease](01-glossary.ko.md#owner-lease) token을 기록한다. | Host lease가 만료되면 더 이상 유효하지 않은 ephemeral data로 처리한다. |
| Host owner lease | `(OwnerId, LeaseGeneration, ExpiresAt, StoreNow)`를 기록한다. | Redis TTL이 만료 여부를 결정한다. |
| Object authority | Canonical authority key, opaque payload, `StoreVersion`, `ObjectGeneration`, `AuthorityOwnerGeneration`, current `OwnerId`·`LeaseGeneration`과 Pending·Active placement allocation을 기록한다. | 명시적인 fenced delete가 성공할 때까지 유지하며 TTL을 설정하지 않는다. |
| Authority index | Snapshot scan에 사용하는 versioned key index와 active scan lease를 기록한다. | Current index는 authority row를 따르고, 삭제 이력은 이를 참조할 수 있는 scan lease가 끝나거나 만료될 때까지 유지한다. |
| Creation attempt | Object key, stable type, target descriptor, capacity 변화량, exact authority fence와 terminal result를 기록한다. | Reserved 상태는 exact fence로 takeover 또는 terminal 처리할 때까지 TTL 없이 유지한다. Created·Rejected·Aborted terminal 상태는 caller 결과 회수를 위한 retention TTL 동안 유지한다. |
| Relocation capacity reservation | Current authority key·`StoreVersion`, source·target descriptor와 owner token, capacity 변화량을 기록한다. | `NewOwner` 또는 aggregate commit이나 exact abort가 끝날 때까지 유지하며 TTL을 설정하지 않는다. |
| Global counters | `ObjectGeneration`, `AuthorityOwnerGeneration`, `StoreRevision`, `LeaseGeneration`을 transaction domain 전체에서 발급한다. | Provider transaction domain에 durable counter로 유지한다. |

### 2.1 공통 물리 schema

공식 Redis extension은 언어와 관계없이 같은 hybrid schema를 사용한다. Authority current state와
scan history는 authority별 HASH에 두고, transaction domain 전체가 공유하는 counter·capacity·membership·
versioned index만 shared HASH 또는 ZSET에 둔다. Creation attempt, relocation fence와 aggregate record는
각 operation별 HASH로 분리한다. 여러 authority의 property를 몇 개의 큰 HASH로 나누는 layout과 provider
전체 state를 하나의 serialized value로 저장하는 layout은 사용하지 않는다.

사용자가 설정하는 prefix `P`에는 `{`와 `}`를 허용하지 않는다. Provider는 모든 transaction key에 literal
`{zlink-location-v1}` hash tag를 넣는다. 다음 표의 `D`는 canonical key bytes의 SHA-256 lower-hex이고,
`I`는 UUID의 32자 lower-hex다.

| 용도 | Redis key와 자료형 |
|---|---|
| Schema marker | `P:{zlink-location-v1}:schema` · HASH |
| Global counter | `P:{zlink-location-v1}:counter` · HASH |
| Host owner lease | `P:{zlink-location-v1}:owner-lease:D` · HASH |
| Public [MeshNode](01-glossary.ko.md#meshnode) descriptor | `P:{zlink-location-v1}:descriptor:mesh:D` · HASH |
| Descriptor admission metadata | `P:{zlink-location-v1}:descriptor-admission:mesh:D` · HASH |
| Descriptor canonical key index | `P:{zlink-location-v1}:descriptor:mesh:index` · SET |
| Descriptor [owner](01-glossary.ko.md#owner) token index | `P:{zlink-location-v1}:descriptor:mesh:owner:D` · SET |
| Current authority | `P:{zlink-location-v1}:authority:current:D` · HASH |
| Authority history | `P:{zlink-location-v1}:authority:history:D` · HASH |
| Authority revision index | `P:{zlink-location-v1}:authority:history-revisions:D` · ZSET |
| Canonical authority key index | `P:{zlink-location-v1}:authority:key-index` · ZSET |
| Deleted key GC index | `P:{zlink-location-v1}:authority:index-gc` · ZSET |
| Current [membership](01-glossary.ko.md#membership) | `P:{zlink-location-v1}:membership:current` · HASH |
| Membership history | `P:{zlink-location-v1}:membership:history:D` · HASH |
| Membership revision index | `P:{zlink-location-v1}:membership:history-revisions:D` · ZSET |
| Node capacity | `P:{zlink-location-v1}:capacity:node:<active\|pending>` · HASH |
| Type capacity | `P:{zlink-location-v1}:capacity:type:<active\|pending>` · HASH |
| Creation attempt | `P:{zlink-location-v1}:creation:I` · HASH |
| Relocation capacity fence | `P:{zlink-location-v1}:relocation:I` · HASH |
| Aggregate record | `P:{zlink-location-v1}:aggregate:I:<generation>` · HASH |
| Scan lease | `P:{zlink-location-v1}:scan:I` · HASH |
| Scan expiry index | `P:{zlink-location-v1}:scans:expiry` · ZSET |
| Scan watermark index | `P:{zlink-location-v1}:scans:watermark` · ZSET |

Schema marker의 `format`은 `location-authority-hybrid-v1`이고 최초 `epoch`은 decimal `1`이다. 비어 있는
transaction domain만 이 marker로 초기화할 수 있다. Marker가 없는데 domain key가 있거나 format이 다르면
startup을 실패한다. Migration은 serving을 닫은 상태에서 전체 invariant와 digest를 검증하고 새 epoch를
마지막 CAS로 publish한다. Provider는 다른 layout을 자동으로 읽거나 dual-write하지 않는다.

Current authority HASH는 원래 canonical authority key bytes를 함께 저장하고 `D`와 exact match를 확인한다.
그 밖에 `payload`, `storeVersion`, `objectGeneration`, `authorityOwnerGeneration`, `ownerId`,
`ownerLeaseGeneration`, `allocationState`, `objectKind`, `stableType`, `descriptorKey`,
`descriptorLifecycleGeneration`, `capacityDelta` field를 사용한다. Pending allocation은
여기에 `pendingCreationReservationId`, `pendingCreationReference`,
`pendingCreationSha256`, `pendingCreationEncodedSize` 네 field를 반드시 추가한다.
각각 provider가 발급한 reservation ID, 변경할 수 없는 creation content의 reference,
정확히 32 bytes인 SHA-256과 `0..1 MiB` encoded size를 저장한다. Active allocation에는
이 네 field를 두지 않는다. Optional field를 빈 문자열이나 synthetic `0`으로 대신하지
않는다. `objectKind`는 enum의 언어별 이름이나 정수값이 아니라
`actor`, `user_spot`, `instance_spot` 중 하나로 저장한다.

Capacity HASH의 field도 언어에 따라 달라지지 않는 length-prefix encoding을 사용한다. 각 segment는
`<UTF-8 byte length>:<value>`로 encode하고 separator를 추가하지 않는다. Node bucket은 canonical
descriptor key와 lifecycle generation의 decimal 값을 차례로 encode한다. Type bucket은 같은 node
bucket 뒤에 위 `objectKind` token과 [stable type](01-glossary.ko.md#stable-type)을 차례로 encode한다. 따라서 Unicode stable type도
UTF-16 code unit 수가 아니라 UTF-8 byte 수를 사용하며, descriptor lifecycle이 바뀌면 이전 node의
capacity와 새 node의 capacity가 섞이지 않는다.

Public MeshNode descriptor HASH는 공통 fixture의 `owner`, `gen`, `json`, `updatedAtMs`, `mesh` 다섯
field만 가진다. Placement admission에 필요한 descriptor revision, owner lease generation, object role,
runtime state, application version, capability, capacity limit와 immutable digest는 별도 admission HASH에
저장한다. 두 HASH는 descriptor CAS Lua에서 함께 검증하고 변경한다. Provider가 관리하는 active·pending
count의 권한 원본은 capacity HASH이고 public descriptor의 count는 projection이다.

### 2.2 MeshNode의 변경 불가능한 설정을 검증하는 digest

서로 다른 언어의 provider가 같은 MeshNode 설정을 동일한 값으로 검증하려면 각
언어의 descriptor serializer 결과를 그대로 hash해서는 안 된다. Admission HASH의
`immutableDigest`는 아래 값을 정해진 순서와 encoding으로 연결한 canonical
preimage의 SHA-256 lower-hex다.

각 값은 separator 없이 `<UTF-8 byte length>:<value>` 형식으로 이어 붙인다.

| 순서 | Canonical preimage에 넣는 값 |
|---:|---|
| 1 | Domain 문자열 `zlink-mesh-node-immutable-v1`을 넣는다. |
| 2 | MeshName을 넣는다. |
| 3 | RID를 lowercase hex로 변환하여 넣는다. |
| 4 | [Lifecycle generation](01-glossary.ko.md#lifecycle-generation)을 invariant decimal로 넣는다. |
| 5 | Endpoint를 넣는다. |
| 6 | ChannelName 개수와 unsigned UTF-8 byte 순서로 정렬한 각 [ChannelName](01-glossary.ko.md#channelname)을 넣는다. |
| 7 | Security identity를 넣는다. |
| 8 | Application version을 invariant decimal로 넣는다. |
| 9 | Object role을 `none`, `client`, `server` 중 하나의 token으로 넣는다. |
| 10 | Node active limit과 pending limit을 invariant decimal로 넣는다. |
| 11 | Capability 개수와 정해진 순서로 정렬한 각 capability를 넣는다. |

Capability는 `(objectKind token, stableType)` 순서로 정렬한다. 각 capability에는
`objectKind`, `stableType`, relocation policy, [Snapshot](01-glossary.ko.md#snapshot) 지원 여부와
optional type limit을 차례로 넣는다.

| Capability 항목 | Encoding 규칙 |
|---|---|
| `objectKind` | `actor`, `user_spot`, `instance_spot` 중 하나를 사용한다. |
| Relocation policy | `disabled`, `recreate`, `snapshot` 중 하나를 사용한다. |
| Snapshot 지원 여부 | `0` 또는 `1`을 사용한다. |
| Type active·pending limit | 값이 없으면 빈 segment를 사용하고, 있으면 invariant decimal segment를 사용한다. |

Capability 항목 목록도 unsigned UTF-8 byte lexical order로 정렬한다. ChannelName,
capability를 정렬할 때 Unicode normalization을
적용하지 않는다.

다음 값은 같은 lifecycle 안에서 바뀔 수 있거나 별도의 owner 검증 정보로 확인하므로
`immutableDigest`에 넣지 않는다.

- `DescriptorRevision`
- Channel weight와 node placement [weight](01-glossary.ko.md#weight)
- Maintenance wave와 runtime state
- OwnerId와 owner lease generation
- Update 시각
- Active·pending 사용량

모든 provider의 byte-level contract test는 공식 fixture의 canonical preimage와
digest를 그대로 검증해야 한다.

Host owner lease HASH는 `ownerId`, `generation`, `expiresAt` 세 field만 사용하고 key TTL을 함께 설정한다.
`expiresAt`은 Redis server time 기준 Unix epoch millisecond다. 문자열 value나 언어별 legacy lease key를
함께 쓰지 않으며 descriptor와 authority transaction은 이 HASH의 세 field를 직접 검증한다.

Descriptor canonical key index의 member는 원래 canonical descriptor key다. Descriptor owner token index의
`D`는 `ownerId`, NUL byte, lease generation decimal을 차례로 붙인 UTF-8 bytes의 SHA-256 lower-hex이며
member도 canonical descriptor key다. 이 index는 exact owner token cleanup에만 사용하고 owner ID만으로
descriptor를 제거하지 않는다.

Redis script가 접근하는 모든 key는 `KEYS`로 전달한다. `ARGV`, descriptor JSON, authority payload 또는
Redis에 저장된 record에서 key 이름을 만들어 접근하지 않는다. Provider startup은 대표 key에
`CLUSTER KEYSLOT`을 적용해 같은 slot인지 확인한다. Cluster command를 사용할 수 없는 standalone
deployment에서도 prefix의 brace 금지와 literal hash tag 생성 규칙을 같은 방식으로 검증한다.

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

Actor key는 global ActorId 하나, Spot key는 global SpotId 하나로 구성한다. [MeshName](01-glossary.ko.md#meshname)을 key prefix나 uniqueness
scope에 넣지 않는다. Initial placement intent의 MeshName은 opaque authority payload에 둘 수 있지만 current
MeshName·NodeRid·lifecycle generation은 placement allocation의 descriptor metadata에서 얻는다.

Read 결과는 다음 closed union이다.

- `Missing(StoreNow)`: row가 없으며 StoreVersion이나 generation을 만들지 않는다.
- `Found(Payload, StoreVersion, [ObjectGeneration](01-glossary.ko.md#objectgeneration), [AuthorityOwnerGeneration](01-glossary.ko.md#authority-owner-generation), OwnerId,
  OwnerLeaseGeneration, PlacementAllocation, StoreNow)`: current row snapshot을 반환한다.

Missing 결과에 `0`, 빈 문자열이나 synthetic StoreVersion을 넣지 않는다.

### 4.2 Expectation과 mutation

모든 public authority mutation은 Active `Found`의 expected StoreVersion을 명시한다. Missing→Pending 생성은
generic Reserve가 전담한다. Redis Lua/function은 expectation을 먼저 검증한 뒤 global counter 소비, row write와
index write를 한 atomic operation으로 처리한다.

| Transition | 허용 expectation | generation 결과 |
|---|---|---|
| Preserve | Active Found | target owner token 없이 ObjectGeneration, AuthorityOwnerGeneration과 allocation 유지, 새 StoreRevision만 발급 |
| NewOwner | Active Found | exact target owner lease·capacity fence 검증, target owner와 Active allocation 기록, ObjectGeneration 유지, 새 AuthorityOwnerGeneration과 StoreRevision 발급 |
| Delete | Active Found | 새 StoreRevision 발급, current active capacity 감소 뒤 row와 current index entry 제거 |

Authority payload에는 provider generation과 StoreVersion을 중복 encode하지 않는다. Wire fence는 provider metadata와
opaque StoreVersion을 Framework가 조합한다. Redis script는 payload body를 parse하거나 수정하지 않는다.
Put mutation은 Preserve에서 target owner token을 받지 않고 NewOwner에서 exact token을 반드시 받는다.
Redis script는 이 token의 owner lease row를 CAS와 같은 operation에서 확인하고 owner ID·lease generation을
authority metadata에 기록한다. Preserve와 Delete는 authority row에 저장된 current owner token의 lease를 같은
transaction에서 확인한다. Required lease가 missing·stale이면 current authority read를 담은 Conflict를 반환하고
row·index·counter를 변경하지 않는다. Token 존재 규칙을 위반한 mutation은 Redis operation 전에 argument
validation error로 거부한다. Relocation capacity fence는 NewOwner에서만 반드시 있고 Preserve에서는
없어야 한다. 이 조합도 Redis operation 전에 거부한다.

Framework가 encode하는 authority payload는 relocation phase, `RelocationId`, immutable
source와 current target fence, Relocation Store root reference와 checksum, membership, replay·completion count를
포함한다. Redis provider는 이 field를 해석하지 않지만 expected StoreVersion CAS는 reference, checksum, phase,
membership과 count를 하나의 authority revision으로 바꾼다. Current owner ID·lease generation과 placement
allocation의 state·kind·stable type·descriptor key·lifecycle generation·capacity delta는 provider metadata에만
두며 payload에 중복 encode하지 않는다. 일부 field만 별도 operation으로 갱신하지 않는다.

Logical create와 ObjectGeneration·initial allocation 발급은 아래 generic placement reservation operation만 사용한다.
Public authority CAS는 Missing row를 만들지 않는다.

Missing→Pending은 `Reserve`, Actor Pending→Active는 exact
`CompleteCreation(Created)`, Actor Pending→Missing은 exact
`CompleteCreation(Rejected|Failed)` 또는 recovery `Abort`만 수행한다.
Active→다른 Active는 capacity fence를 소비하는 NewOwner·aggregate commit, Active→Missing은 Delete만 수행한다.
Pending row에 Preserve·NewOwner·Delete를 적용하면 Conflict이고 mutation은 0이다.

### 4.3 Generic placement reservation

Provider는 `Reserve`, `CompleteCreation`, `ReadCreationTerminal`과 recovery
`Abort` closed operation을 제공한다.
Provider는 object kind와 stable type을 placement allocation·capability·capacity
counter를 선택하는 metadata로 처리하지만 Framework가 encode한
application·creation·relocation payload는 해석하지 않는다. Reservation은 object
kind, global canonical key, stable type, immutable creation intent reference·hash,
target descriptor key·lifecycle generation, active·pending capacity delta, exact owner
fence와 Framework가 encode한 opaque Creating authority payload를 가진다.

`CompleteCreation`은 `Created`, `Rejected` 또는 `Failed` outcome, Framework가
encode한 opaque semantic terminal envelope와 Created의 Ready authority payload를 받는다. Redis provider는
payload를 해석하거나 합성하지 않는다. Reserved attempt에는 TTL을 두지 않으며
Creating authority와 target host owner lease를 기준으로 recovery, takeover 또는
terminal 처리를 수행한다.

| Operation | Atomic mutation |
|---|---|
| `Reserve` | `Missing → Creating`, generation 발급, creation intent 연결과 target pending capacity 증가 |
| `CompleteCreation(Created)` | Target descriptor lifecycle·owner lease 재검증, exact `Creating → Ready`, pending 감소, active 증가와 operation-scoped `Created` terminal publish |
| `CompleteCreation(Rejected\|Failed)` | Exact Creating authority 삭제, pending 감소와 operation-scoped terminal publish. Ready authority와 active capacity는 만들지 않음 |
| `ReadCreationTerminal` | Exact source lifecycle·`OperationId`로 retained semantic terminal을 읽거나 Missing 반환 |
| `Abort` | Current lifecycle·lease와 무관하게 reservation에 고정한 exact Creating authority 삭제와 이전 target pending 감소. Operation terminal은 publish하지 않음 |

Expectation, global counter, authority row·index, creation attempt와 capacity counter는
같은 server-side transaction에서 검증하고 변경한다. Capacity HASH는 node별 Actor
active·reserved count, User·Instance Spot 전체 count와 Spot stable type별 count의
권한 원본이다. Limit `0`은 제한 없음이며 양수 limit은 Active와 reserved delta를 합해
검사한다. Entry Spot은 이 counter에 포함하지 않는다. `Reserve`가 limit을 넘으면
아무 값을 소비하지 않고 `PlacementCapacityExhausted`를 반환한다.

Created completion은 target descriptor lifecycle과 owner lease가 stale이면 mutation 0으로
끝내고 reservation을 유지한다. Completion은 exact operation terminal key를 CAS하며 이미
publish한 terminal을 다른 outcome으로 바꾸지 않는다. Abort는 reservation에 기록한 이전
descriptor·counter를 정리한다. Stale reservation은 새
authority나 capacity를 변경하지 못한다.

Creation request는 encoded 최대 1 MiB이고 immutable content reference와 hash로
저장한다. Ready 또는 fenced terminal 처리가 확정될 때까지 reference를 유지한다.
Provider는 content를 해석하지 않는다. Recovery는 owner lease가 stale한 Reserved
attempt를 exact fence로 takeover하거나 abort하며, elapsed time만으로 Reserved
attempt를 삭제하지 않는다.

[Creation attempt](01-glossary.ko.md#creation-attempt) HASH는 Reserved 또는 terminal
상태 중 하나만 가진다.

```text
Missing
  → Reserved(R1)
      ├─ Created(R1, ObjectRef, ReplyRef?)
      ├─ Rejected(R1, ReplyRef?)
      └─ Aborted(R1, Failure)
```

Created와 Rejected는 reservation winner operation의 정상 terminal result이며 callback
exception은 Failed다. Abort는 recovery cleanup이므로 operation terminal이 아니다.
Creating을 관찰한 서로 다른 operation은 Ready 뒤 Existing을 받고 cleanup 뒤 새
reservation을 경쟁한다. 앞선 application reply를 공유하지 않는다.

Terminal transition은 `(source Node RID raw bytes, source lifecycle generation,
OperationId)` exact key의 HASH에 outcome, correlation-free
`creation-operation-terminal-v1` envelope, SHA-256과 Redis server time 기준 expiry를
기록한다. 같은 source lifecycle·`OperationId`의 재전송만 이 record를 읽고 Framework가
현재 correlation·reply route로 command reply를 다시 encode한다. TTL은 original
operation deadline 뒤 5분이며 Reserved 상태에는 TTL을 설정하지 않는다.

Creation terminal key의 RID segment는 transport `RoutingId`의 exact raw bytes 길이와
그 raw bytes의 lowercase hex다. RoutingId를 먼저 canonical hex text로 바꾼 뒤 그
문자열을 UTF-8로 다시 encode하지 않는다. 예를 들어 raw bytes가 `node-a`이면 segment는
`6:6e6f64652d61`이다.

#### Relocation capacity reservation

Existing object relocation은 creation reservation을 재사용하지 않는다. `ReserveRelocationCapacity`는 Framework가
만든 non-zero 128-bit reservation ID, current authority key·StoreVersion, kind·stable type, source
descriptor key·lifecycle generation·owner token, target descriptor key·lifecycle generation·owner token과
capacity delta를 받는다. Lua/function은 request source identity가 current authority owner와 durable Active
placement allocation의 descriptor key·lifecycle generation·kind·stable type·capacity delta와 정확히 같은지
확인한다. Source descriptor row와 source owner lease의 live 상태는 요구하지 않는다. Target descriptor
lifecycle·owner lease·capability·pending limit만 live/exact로 확인하고 target pending을 예약한다. Authority row와
source active count는 이 단계에서 바꾸지 않는다. 같은 ID와 exact request는 같은
fence를 반환하고 다른 request는 conflict다.

Standalone `NewOwner` CAS는 relocation capacity fence를 필수로 받아 authority owner 전환, source active 감소,
target pending 감소·active 증가와 fence commit을 한 transaction에서 처리한다. Aggregate commit도 participant별
relocation capacity fence를 같은 transaction에서 소비한다. Abort는 uncommitted fence의 target pending만 해제하며
반복 abort는 idempotent하고 committed 또는 다른 fence는 closed result로 구분한다. Reservation에는 TTL을 두지
않는다. Recovery는 expected authority StoreVersion, current owner token과 Active allocation을 사용하며 payload를 해석하지 않는다.
Standalone NewOwner fence가 reserved 상태가 아니거나 authority key·expected StoreVersion·source·target owner와
일치하지 않으면 current authority read를 담은 Conflict이며 authority row, capacity와 fence state의 mutation은
0이다. 이미 committed·aborted된 fence도 같다. CAS script는 request source와 durable Active allocation의 exact
match를 다시 확인하고 target descriptor lifecycle과 target owner lease만 live/exact로 재검증한다. Source
descriptor row·lease가 stale·missing이어도 allocation match가 유지되면 commit할 수 있다. Target이 stale이면
같은 Conflict와 mutation 0으로 끝낸다.
Aggregate prepare에서 fence reservation record와 `NewOwner` participant는 정확히 일대일이어야 한다. Fence의
authority key·expected StoreVersion·source owner·target owner가 participant expectation, current authority owner,
aggregate target owner와 일치해야 한다. Request source는 durable Active allocation과 exact match해야 하고 target
descriptor lifecycle·owner lease만 live/exact로 다시 확인한다.
누락·중복·추가 fence나 값 불일치는 conflict이며 row·capacity·fence·aggregate record를 변경하지 않는다.

### 4.4 Bounded aggregate commit

User Spot relocation과 cross-node Actor `JoinSpot`·`JoinEntrySpot`은 generic bounded aggregate transaction을 사용한다. Request는 non-zero
128-bit aggregate ID, exact aggregate generation, participant별 authority key·expected StoreVersion·owner mutation과
membership mutation을 가진다. Participant는 최대 1024개이고 encoded request와 aggregate record는 각각 최대
1 MiB다. Provider는 [User Spot](01-glossary.ko.md#entry-user-instance-spot)이나 Actor 의미를 해석하지 않고 expectation과 mutation vector만 처리한다.

[Location Store](01-glossary.ko.md#location-store)의 aggregate record가 bounded canonical participant set, participant별 mutation, aggregate generation과
inventory digest를 권한 원본으로 저장한다. Relocation Store manifest는 participant별 state·journal payload를 찾기
위한 같은 digest의 projection일 뿐 authority가 아니다. Location Store transaction은 Relocation Store를 호출하거나
두 Store 사이 2PC를 수행하지 않는다.

`PrepareAggregate`는 모든 participant expectation, `NewOwner` participant와 일대일로 대응하는 relocation capacity
fence와 owner lease fence를 확인하고 durable prepared record를 만든다. 같은 transaction에서 각 Reserved fence를
aggregate ID·generation에 bind하고 Prepared로 전이한다. Bind된 fence의 direct abort는 Stale이고 다른 aggregate
prepare는 conflict다. Exact duplicate prepare만 idempotent하다. `CommitAggregate`는 prepared record의 exact
generation에 bind된 fence의 source Active allocation match와 target descriptor lifecycle·owner lease를 다시
확인한다. Source descriptor row·lease가 stale·missing이어도 allocation match가 유지되면 commit할 수 있다.
Target이 stale이면 participant·capacity·fence를 바꾸지 않고 bind 상태를 유지한다. 유효한 fence만 소비해 모든 authority owner,
AuthorityOwnerGeneration, membership index와 aggregate commit generation을 한 server-side transaction으로
전환한다. `AbortAggregate`는 commit 전 prepared record와 bind된 fence의 target pending을 함께 정리하고 fence를
aborted로 닫는다. 같은 aggregate generation의
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

첫 page는 current StoreRevision을 watermark로 고정하고 scan lease를 만든 뒤 즉시 bounded page를 읽는다.
Authority key index의 member는 canonical key bytes의 lower-hex이고 score는 `0`이다. `ZRANGEBYLEX`로 마지막
key 다음부터 최대 1000개 candidate만 읽는다. Base64url은 byte lexical order를 보존하지 않으므로 index
member encoding으로 사용하지 않는다.

Mutation은 active scan이 참조할 수 있는 이전 revision을 overwrite하거나 delete하기 전에 immutable full
snapshot 또는 tombstone을 authority history에 저장하고, 64-bit StoreRevision의 16자리 lower-hex를 score
`0`인 revision index member로 추가한다. Revision을 ZSET score에 넣지 않는다. Redis score는
IEEE-754 double이므로 `2^63-1`까지 exact StoreRevision을 표현할 수 없기 때문이다.

Authority history HASH의 field encoding도 공통이다. Revision hex를 `R`이라 할 때
Active full snapshot은 `R:deleted=0`과 `R:<current field name>` 13개 field를
저장한다. Pending full snapshot은 같은 13개 field와 네
`R:pendingCreation*` field를 모두 저장하므로 current row와 같은 17개 field를
복원한다. Pending snapshot에서 네 field가 하나라도 빠지거나 Active snapshot에
하나라도 있으면 schema violation이다. Tombstone은 `R:deleted=1`과
`R:authorityKey`만 저장하며 StoreVersion은 `R`에서 복원한다. 하나의 language
runtime만 해석할 수 있는 serialized JSON value나 다른 field grouping을 사용하지
않는다. Membership history는 같은 `R`을 field로, immutable membership bytes를
value로 저장한다.

Page provider는 먼저 bounded key candidate를 얻고 client에서 current/history key를 계산한 뒤 모든 key를
명시적인 `KEYS`로 전달하는 bounded Lua를 실행한다. Lua는 scan ID, watermark, last key, cursor ordinal과
expiry를 검증하고 watermark 이하의 마지막 immutable revision을 선택한다. Watermark 뒤 create와
watermark 이하 delete tombstone은 제외한다. 반환 item 1000개 또는 encoded 4 MiB 중 먼저 도달한 제한에서
멈춘다. Cursor는 compare-and-advance하며 replay한 cursor는 `ScanExpired`다.

Scan expiry와 watermark index는 만료된 lease와 history를 bounded worker가 정리할 때 사용한다. 최소 active
watermark보다 새 revision을 삭제하지 않는다. Delete한 authority key index member도 해당 delete revision보다
오래된 scan이 모두 끝난 뒤에만 제거한다. Cleanup 지연은 history retention만 늘리고 read correctness를
바꾸지 않는다.

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
Object Server descriptor의 exact Entry Spot ID도 lifecycle 동안 immutable하다. Replacement lifecycle은 새
descriptor와 새 Entry Spot ID mapping을 게시한다.
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
보장한다. RID는 Framework가 `prefix-<lowercase-canonical-uuid-v4>` 형식으로 생성하며 Redis provider는 RID
구조나 prefix를 해석하지 않는다. Claim은 exact host owner lease token과 descriptor identity를 함께
기록하고, renew·update·release는 같은 token을 비교한다.

Active conflict는 기존 descriptor를 덮어쓰지 않는다. Framework는 새 UUID나 두 번째 claim을 만들지 않고
`SpotIdConflict`로 startup을 즉시 실패한다. Replacement lifecycle은 새 UUID 기반 Spot ID로 새 claim을 수행한다.

Entry Spot ID는 global Spot authority namespace에서 충돌을 확인한다. Framework는
`<prefix>-entry-<lowercase-canonical-uuid-v4>` candidate를 만든다. Active conflict가 확인되면 새 UUID나
reservation을 만들지 않는다. Provider는 prefix, marker나 UUID를 해석하지 않는다. Claim에 성공한 exact
RID는 MeshNode descriptor의 같은 lifecycle mapping에 기록한다. Caller가 지정한 User·Instance Spot ID가
예약 형식과 일치하는지는 Framework가 Store operation 전에 검사한다.

## 8. Store 장애와 recovery

`StoreFailureGrace`는 descriptor discovery reconcile과 새 outbound connect에만 적용한다. 마지막 stable desired set은
grace 동안 유지할 수 있고 existing transport는 service liveness를 계속 적용한다. Grace가 끝난 뒤 stable page
snapshot을 다시 얻기 전에는 새 connection을 만들지 않는다.

Grace는 host owner lease, coordinator lease와 local authority deadline을 연장하지 않는다. 마지막 valid owner lease
read에서 계산한 monotonic [deadline](01-glossary.ko.md#deadline)에 도달하면 Actor·[Spot](01-glossary.ko.md#spot)·Instance message, timer, factory completion, relocation
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

## 10. 구현 및 contract test 검증 요구

- Missing read가 StoreNow만 반환하고 synthetic StoreVersion과 generation을 만들지 않는다.
- ActorId와 SpotId가 MeshName과 독립적인 global authority key로 저장된다.
- Reserve가 exact target owner lease를 검증하고 initial generation·Pending allocation·row/index를 한
  operation으로 만들며 NewOwner가 exact target owner lease와 relocation capacity fence를 검증하고 Active
  allocation·owner metadata를 한 operation으로 교체한다.
- 모든 generation counter가 `2^63-1`에서 `GenerationExhausted`를 stable하게 반환하고 아무 값도 소비하지 않는다.
- Authority row에 TTL이 없고 host owner lease 만료만으로 row가 삭제되지 않는다.
- `RemoveAllByOwner`가 exact host token의 ephemeral descriptor만 제거하고 durable authority·reservation은 유지한다.
- Authority scan이 1000 item·4 MiB·4096-byte opaque cursor와 snapshot consistency를 지킨다.
- Descriptor page가 1000 item·4 MiB를 지키고 unstable scope stamp 결과를 적용하지 않는다.
- Oversize descriptor와 capability vector가 startup을 실패시키며 partial descriptor를 publish하지 않는다.
- Descriptor owner CAS가 active RID conflict를 덮어쓰지 않으며 exact host lease fence를 사용한다.
- Object Server descriptor가 같은 lifecycle 동안 immutable한 exact Entry Spot ID mapping을 유지하고
  replacement lifecycle은 새 mapping을 게시한다.
- Entry Spot authority 충돌은 두 번째 reservation 없이 즉시 실패하며, 예약 형식의 caller-provided Spot
  RID는 Redis operation 전에 거부한다.
- `Reserve`, `CompleteCreation`, `ReadCreationTerminal`, `Abort`가 authority,
  operation terminal과 node·type capacity를 계약대로 전환한다.
- Concurrent `GetOrCreate`에서 CAS winner만 factory와 creation callback을 실행하고,
  다른 operation은 authority 변경을 기다린다.
- `CompleteCreation(Created)`가 Ready authority, active capacity와 terminal result를 하나의
  transaction으로 publish한다.
- `CompleteCreation(Rejected|Failed)`가 Ready authority와 active capacity를 만들지
  않고 reserved capacity를 반환하면서 operation-scoped terminal을 publish한다.
- 서로 다른 operation은 application reply를 공유하지 않고, 같은 source
  lifecycle·`OperationId`만 5분 retention 동안 semantic terminal을 replay한다.
- Creation terminal RID key segment가 exact RoutingId raw bytes를 사용한다.
- Found·Stored·scan snapshot이 provider-owned Pending·Active allocation metadata를 완전하게 반환한다.
- Relocation reserve·standalone commit·aggregate commit이 request source를 durable Active allocation에
  exact-match하고 source descriptor·lease stale recovery는 허용하되 target stale commit은 no-write로 막는다.
- Aggregate prepare가 Reserved capacity fence를 aggregate ID·generation에 atomic하게 bind하며 bind된 fence의
  direct abort와 다른 aggregate prepare를 거부한다.
- Delete가 live current owner lease와 Active allocation을 검증하고 exact active capacity delta를 row와 함께
  atomic하게 제거한다.
- Pending reservation recovery가 elapsed time이 아니라 exact owner lease와 authority fence를 사용한다.
- Creation request reference와 hash가 Ready 또는 fenced abort까지 유지되고 encoded 1 MiB bound를 지킨다.
- Pending authority의 네 `pendingCreation*` field와 history의 17개 field가
  reservation ID, creation reference, SHA-256과 encoded size를 빠짐없이 복원한다.
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
