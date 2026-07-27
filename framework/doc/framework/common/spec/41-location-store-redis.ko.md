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

Session socket, binding token과 Actor별 binding route(현재 Actor owner에 전달할 경로)는 Location Store record나
transaction에 포함하지 않는다. Actor relocation이 `Completed`까지 끝난 뒤 route를
바꾸는 작업은 Store provider가 아니라 Session owner Framework runtime이 수행한다.

Redis server time이 lease 만료의 기준이다. Application host의 wall clock은
[authority](01-glossary.ko.md#authority) 판단에 사용하지 않는다.

공식 extension의 Location provider class는 descriptor, owner lease, authority, placement와 aggregate
operation을 모두 포함하는 하나의 Location Store interface를 구현한다. Descriptor·lease·authority별 public
interface와 부분 provider 구성은 제공하지 않는다. Relocation payload provider는 별도의 Relocation Store
interface와 class로 유지한다. Redis Location provider는 change stamp를 구현해 반환하지만, 공통 Location Store
interface의 기본값은 `null`이며 다른 provider가 stamp를 지원해야 할 의무는 없다.

Root 없는 `Preparing` authority를 steady payload로 되돌리는 recovery primitive는 선택 기능이 아니다. 공식
Redis provider는 exact StoreVersion과 stored owner token을 하나의 script에서 검증한 뒤 authority payload를
바꾼다. 이 operation은 통합 Location Store interface에 포함되며 별도 recovery capability를 노출하지 않는다.

## 2. 저장 영역과 수명

Redis key prefix는 배포 단위에서 설정할 수 있지만 같은 provider transaction domain에서는 다음 논리 영역을
분리한다.

| 영역 | 값 | 수명 |
|---|---|---|
| Discovery [descriptor](01-glossary.ko.md#descriptor) | [MeshNode descriptor](01-glossary.ko.md#meshnode-descriptor), [ClientServer Server descriptor](01-glossary.ko.md#clientserver-server-descriptor) 또는 [fanout publisher descriptor](01-glossary.ko.md#fanout-publisher-descriptor)와 host [owner lease](01-glossary.ko.md#owner-lease) token을 기록한다. | Host lease가 만료되면 더 이상 유효하지 않은 ephemeral data로 처리한다. |
| Entry Spot ID claim | Object Server descriptor의 `EntrySpotId`, descriptor lifecycle과 exact owner lease token을 기록한다. | Descriptor `NewClaim`에서 생성하고 exact descriptor remove·owner cleanup에서 해제한다. |
| Host owner lease | `(OwnerId, LeaseGeneration, ExpiresAt, StoreNow)`를 기록한다. | Redis TTL이 만료 여부를 결정한다. |
| Object authority | Canonical authority key, opaque payload, `StoreVersion`, `ObjectGeneration`, `AuthorityOwnerGeneration`, current `OwnerId`·`LeaseGeneration`과 Reserved·Active placement allocation을 기록한다. | 명시적인 fenced delete가 성공할 때까지 유지하며 TTL을 설정하지 않는다. |
| Authority index | Snapshot scan에 사용하는 versioned key index와 active scan lease를 기록한다. | Current index는 authority row를 따르고, 삭제 이력은 이를 참조할 수 있는 scan lease가 끝나거나 만료될 때까지 유지한다. |
| Creation reservation | Object key, stable type, target descriptor, typed capacity bundle과 exact authority fence를 기록한다. | Creating recovery 또는 terminal cleanup까지 durable하게 유지한다. |
| Creation operation terminal | Exact source lifecycle·`OperationId`, reservation fence, `Created`·`Rejected` state와 opaque application reply envelope를 기록한다. | 같은 operation의 retry retention이 끝날 때까지 durable하게 유지한다. |
| Relocation capacity reservation | Current authority key·`StoreVersion`, source·target descriptor와 owner token, typed capacity bundle을 기록한다. | Standalone `NewOwner` 또는 exact abort까지 durable하게 유지하며 TTL을 설정하지 않는다. |
| Global counters | `ObjectGeneration`, `AuthorityOwnerGeneration`, `StoreRevision`, `LeaseGeneration`을 transaction domain 전체에서 발급한다. | Provider transaction domain에 durable counter로 유지한다. |

### 2.1 공통 물리 schema

공식 Redis extension은 언어와 관계없이 같은 hybrid schema를 사용한다. Authority current state와
scan history는 authority별 HASH에 두고, transaction domain 전체가 공유하는 counter·capacity·membership·
versioned index만 shared HASH 또는 ZSET에 둔다. Creation reservation, relocation fence와 aggregate record는
각 operation별 HASH로 분리한다. 여러 authority의 property를 몇 개의 큰 HASH로 나누는 layout과 provider
전체 state를 하나의 serialized value로 저장하는 layout은 사용하지 않는다.

사용자가 설정하는 prefix `P`에는 `{`와 `}`를 허용하지 않는다. Provider는 모든 transaction key에 literal
`{zlink-location-v3}` hash tag를 넣는다. 다음 표의 `D`는 canonical key bytes의 SHA-256 lower-hex,
`E`는 full Entry Spot ID UTF-8 bytes의 SHA-256 lower-hex이고 `I`는 reservation·aggregate UUID raw
128-bit 값의 32자 lower-hex다.

| 용도 | Redis key와 자료형 |
|---|---|
| Schema marker | `P:{zlink-location-v3}:schema` · HASH |
| Global counter | `P:{zlink-location-v3}:counter` · HASH |
| Host owner lease | `P:{zlink-location-v3}:owner-lease:D` · HASH |
| Public [MeshNode](01-glossary.ko.md#meshnode) descriptor | `P:{zlink-location-v3}:descriptor:mesh:D` · HASH |
| Descriptor admission metadata | `P:{zlink-location-v3}:descriptor-admission:mesh:D` · HASH |
| Descriptor canonical key index | `P:{zlink-location-v3}:descriptor:mesh:index` · SET |
| Descriptor [owner](01-glossary.ko.md#owner) token index | `P:{zlink-location-v3}:descriptor:mesh:owner:D` · SET |
| Entry Spot ID claim | `P:{zlink-location-v3}:entry-spot-id:E` · HASH |
| Current authority | `P:{zlink-location-v3}:authority:current:D` · HASH |
| Authority history | `P:{zlink-location-v3}:authority:history:D` · HASH |
| Authority revision index | `P:{zlink-location-v3}:authority:history-revisions:D` · ZSET |
| Canonical authority key index | `P:{zlink-location-v3}:authority:key-index` · ZSET |
| Deleted key GC index | `P:{zlink-location-v3}:authority:index-gc` · ZSET |
| Current [membership](01-glossary.ko.md#membership) | `P:{zlink-location-v3}:membership:current` · HASH |
| Membership history | `P:{zlink-location-v3}:membership:history:D` · HASH |
| Membership revision index | `P:{zlink-location-v3}:membership:history-revisions:D` · ZSET |
| Actor capacity | `P:{zlink-location-v3}:capacity:actor:<active\|reserved>` · HASH |
| Spot capacity | `P:{zlink-location-v3}:capacity:spot:<active\|reserved>` · HASH |
| Spot type capacity | `P:{zlink-location-v3}:capacity:spot-type:<active\|reserved>` · HASH |
| Creation reservation | `P:{zlink-location-v3}:creation:I` · HASH |
| Relocation capacity fence | `P:{zlink-location-v3}:relocation:I` · HASH |
| Aggregate record | `P:{zlink-location-v3}:aggregate:I:<generation>` · HASH |
| Scan lease | `P:{zlink-location-v3}:scan:I` · HASH |
| Scan expiry index | `P:{zlink-location-v3}:scans:expiry` · ZSET |
| Scan watermark index | `P:{zlink-location-v3}:scans:watermark` · ZSET |

Schema marker의 `format`은 `location-authority-hybrid-v3`이고 최초 `epoch`은 decimal `3`이다. 비어 있는
transaction domain만 이 marker로 초기화할 수 있다. Marker가 없는데 domain key가 있거나 format이 다르면
startup을 실패한다. Migration은 serving을 닫은 상태에서 전체 invariant와 digest를 검증하고 새 epoch를
마지막 CAS로 publish한다. Provider는 다른 layout을 자동으로 읽거나 dual-write하지 않는다.
기존 `location-authority-hybrid-v1` 또는 `location-authority-hybrid-v2` domain은 offline migration으로
authority allocation과 reservation의 scalar capacity를 typed bundle로 변환하고 모든 Spot canonical
key와 Entry claim이 유효한 UTF-8 Spot ID인지 검증한 뒤에만 epoch 3을 publish할 수 있다. 임의 binary
Spot RID는 자동 변환하지 않으며 migration에서 명시적인 replacement Spot ID가 없으면 실패한다.

Current authority HASH는 원래 canonical authority key bytes를 함께 저장하고 `D`와 exact match를 확인한다.
그 밖에 `payload`, `storeVersion`, `objectGeneration`, `authorityOwnerGeneration`, `ownerId`,
`ownerLeaseGeneration`, `allocationState`, `objectKind`, `stableType`, `descriptorKey`,
`descriptorLifecycleGeneration`, `capacityBundle` field를 사용한다. Reserved allocation은
여기에 `pendingCreationReservationId`, `pendingCreationReference`,
`pendingCreationSha256`, `pendingCreationEncodedSize` 네 field를 반드시 추가한다.
각각 provider가 발급한 reservation ID, 변경할 수 없는 creation content의 reference,
정확히 32 bytes인 SHA-256과 `0..1 MiB` encoded size를 저장한다. Active allocation에는
이 네 field를 두지 않는다. Optional field를 빈 문자열이나 synthetic `0`으로 대신하지
않는다. `objectKind`는 enum의 언어별 이름이나 정수값이 아니라
`actor`, `user_spot`, `instance_spot` 중 하나로 저장한다.

Entry Spot ID claim HASH는 `state`, `spotId`, `descriptorKey`, `descriptorLifecycleGeneration`,
`ownerId`, `ownerLeaseGeneration` 여섯 field만 사용한다. `state`는 case-sensitive ASCII
`Claimed`이고 `spotId`는 full Entry Spot ID의 exact UTF-8 bytes다. Generation은 선행 `0` 없는
invariant decimal이다. Optional field, descriptor JSON 또는 provider 전용 metadata를 추가하지 않는다.
이 HASH에는 TTL을 설정하지 않으며 exact release transaction으로만 삭제한다.

Capacity HASH의 field도 언어에 따라 달라지지 않는 length-prefix encoding을 사용한다. 각 segment는
`<UTF-8 byte length>:<value>`로 encode하고 separator를 추가하지 않는다. Node bucket은 canonical
descriptor key와 lifecycle generation의 decimal 값을 차례로 encode한다. Spot type bucket은 같은 node
bucket 뒤에 `user_spot` 또는 `instance_spot` token과 [stable type](01-glossary.ko.md#stable-type)을 차례로 encode한다. 따라서 Unicode stable type도
UTF-16 code unit 수가 아니라 UTF-8 byte 수를 사용하며, descriptor lifecycle이 바뀌면 이전 node의
capacity와 새 node의 capacity가 섞이지 않는다.

`capacityBundle`은 domain `zlink-capacity-bundle-v2`, Actor slot, Spot slot, Spot type presence를
차례로 length-prefix segment로 기록한 뒤, entry가 있으면 `(objectKind token, stableType, slot 수)`를
이어 붙인다. Slot과 presence는 선행 `0` 없는 invariant decimal이며 presence는 `0` 또는 `1`이다.
Spot type entry의 `objectKind`는 `user_spot` 또는 `instance_spot`만 허용한다. 같은 kind·stable type의
중복 entry, 음수 slot, 모든 slot이 0인 bundle은 provider I/O 전에 argument validation error로 거부한다.

Creation reservation, standalone relocation reservation과 aggregate record는 다음 exact field set을
사용한다. Field 이름은 아래 ASCII spelling과 대소문자를 그대로 사용하며 optional field나 provider
전용 field를 추가하지 않는다.

| Record | Exact HASH fields |
|---|---|
| Creation reservation | `state`, `reservationId`, `authorityKey`, `storeVersion`, `objectGeneration`, `authorityOwnerGeneration`, `reservationVersion`, `objectKind`, `stableType`, `targetDescriptorKey`, `targetDescriptorLifecycleGeneration`, `targetOwnerId`, `targetOwnerLeaseGeneration`, `creationReference`, `creationSha256`, `creationEncodedSize`, `capacityBundle` |
| Creation operation terminal | `state`, `sourceNodeRid`, `sourceNodeGeneration`, `operationIdHigh`, `operationIdLow`, `reservationId`, `objectKind`, `terminalEnvelope`, `terminalEnvelopeSha256`, `expiresAtUnixMs` |
| Standalone relocation | `state`, `reservationId`, `authorityKey`, `expectedStoreVersion`, `objectKind`, `stableType`, `sourceDescriptorKey`, `sourceDescriptorLifecycleGeneration`, `sourceOwnerId`, `sourceOwnerLeaseGeneration`, `targetDescriptorKey`, `targetDescriptorLifecycleGeneration`, `targetOwnerId`, `targetOwnerLeaseGeneration`, `capacityBundle` |
| Aggregate | `state`, `aggregateId`, `aggregateGeneration`, `participants`, `inventoryDigest`, `targetDescriptorKey`, `targetDescriptorLifecycleGeneration`, `targetOwnerId`, `targetOwnerLeaseGeneration`, `capacityBundle` |

Creation reservation의 `state`는 case-sensitive ASCII `Reserved`, `Committed`, `Rejected`, `Aborted`
중 하나다. Creation operation terminal의 `state`는 `Created`, `Rejected`, `Failed` 중 하나다.
Standalone relocation과 aggregate record는 `Reserved`, `Committed`, `Aborted`를 사용한다.
Public prepare 결과의 `Prepared`·`AlreadyPrepared`는 operation 결과 이름이며 durable aggregate
record의 준비 상태는 `Reserved`다. Creation terminal envelope는
`creation-operation-terminal-v1`의 correlation-free semantic bytes다. Command head, request
correlation과 reply route는 포함하지 않으며 Provider는 envelope를 해석하지 않는다.

128-bit ID는 hyphen 없는 32자리 lowercase hex, generation·size는 선행 `0` 없는 decimal,
SHA-256과 inventory digest는 exact 32 raw bytes로 저장한다. Authority key, descriptor key, owner ID,
`StoreVersion`, reference와 stable type은 계약에서 정한 UTF-8 bytes를 그대로 사용한다.
`participants`는 bounded canonical participant set과 mutation을 공통 aggregate encoding으로
serialize한 bytes다.

Creation operation terminal key는 다음 exact 형식이다.

```text
P:{zlink-location-v3}:creation-terminal:<rid-byte-length>:<rid-hex>:<source-generation>:<operation-id-hex>
```

`rid-byte-length`와 `source-generation`은 선행 `0` 없는 decimal이다. `rid-hex`는 Node RID의 exact
UTF-8 bytes를 lowercase hex로 변환한 값이고, `operation-id-hex`는 high·low unsigned 64-bit를 각각
16자리 lowercase hex로 이어 붙인 32자리 값이다. `terminalEnvelope`는 correlation-free semantic
envelope bytes의 lowercase hex projection이며 decode한 bytes가 1,048,576 bytes를 넘으면 provider
I/O 전에 거부한다. `terminalEnvelopeSha256`은 decode한 bytes의 exact SHA-256이다.
`expiresAtUnixMs`는 original operation deadline에 300,000 ms를 더한 값이며 Redis `TIME` 기준으로
이미 만료된 입력은 mutation 전에 거부한다. Terminal key에는 `PEXPIREAT`을 적용하지만 authority와
creation reservation에는 TTL을 적용하지 않는다.

Schema epoch 3 provider는 다음 capacity fixture를 같은 bytes로 생성해야 한다.

```text
actorSlots = 3
spotSlots = 1
spotType = (user_spot, room, 1)
encoded = 24:zlink-capacity-bundle-v21:31:11:19:user_spot4:room1:1
hex = 32343a7a6c696e6b2d63617061636974792d62756e646c652d7632313a33313a31313a31393a757365725f73706f74343a726f6f6d313a31
```

모든 언어 provider의 contract test는 공통 byte fixture를 write한 뒤 raw Redis bytes로 비교하고,
fixture를 raw Redis에 넣은 뒤 public read 결과도 비교한다. Field set, state token,
`capacityBundle` bytes 또는 epoch가 다르면 호환 schema로 읽지 않고 startup을 실패한다.

Entry claim fixture는 full RID `mesh-entry-123e4567-e89b-42d3-a456-426614174000`을 사용한다.
UUID component는 RFC 4122 version 4와 variant를 가진 lowercase canonical `8-4-4-4-12` 형식이다.
이 RID의 `E`는 `52704385274f30e952774ce79876f362d51f5564752cc4d687bbb60bb05d453e`이고
exact key suffix는
`{zlink-location-v3}:entry-spot-id:52704385274f30e952774ce79876f362d51f5564752cc4d687bbb60bb05d453e`다.
`spotId` value hex는
`6d6573682d656e7472792d31323365343536372d653839622d343264332d613435362d343236363134313734303030`,
`state` value hex는 `436c61696d6564`다. Fixture는 여섯 field가 정확히 존재하고 extra field가
없음을 검증한다.

Public MeshNode descriptor HASH는 공통 fixture의 `owner`, `gen`, `json`, `updatedAtMs`, `mesh` 다섯
field만 가진다. Placement admission에 필요한 descriptor revision, owner lease generation, object role,
runtime state, application version, capability, capacity limit와 immutable digest는 별도 admission HASH에
저장한다. 두 HASH는 descriptor CAS Lua에서 함께 검증하고 변경한다. Provider가 관리하는 active·reserved
count의 권한 원본은 capacity HASH이고 public descriptor의 count는 projection이다.
Admission HASH는 `descriptorKey`, `descriptorRevision`, `lifecycleGeneration`, `ownerId`,
`ownerLeaseGeneration`, `objectRole`, `runtimeState`, `applicationVersion`, `capabilities`, `actorLimit`,
`spotLimit`, `activationConcurrencyLimit`, `entrySpotId`, `immutableDigest` 열네 field만 가진다.

### 2.2 MeshNode의 변경 불가능한 설정을 검증하는 digest

Admission HASH의 `immutableDigest`는 언어별 descriptor serializer 결과가 아니라 다음 canonical
preimage의 SHA-256 lower-hex다. 모든 segment는 separator 없이 `<UTF-8 byte length>:<value>`로 이어 붙인다.

1. Domain `zlink-mesh-node-immutable-v2`
2. MeshName
3. Full MeshNode RID
4. Lifecycle generation의 invariant decimal
5. Endpoint
6. ChannelName 개수와 정렬한 각 ChannelName
7. Security identity
8. Application version의 invariant decimal
9. Object role token `none`, `client`, `server` 중 하나
10. Entry Spot ID presence `0` 또는 `1`, 값이 있으면 full Entry Spot ID
11. Node Actor limit, Spot limit과 activation concurrency limit의 invariant decimal
12. Capability 개수와 정렬한 각 capability

Capability는 `(objectKind token, stableType)` 순서로 정렬한다. 각 capability는 `objectKind`, `stableType`,
relocation policy, Snapshot 지원 여부와 Spot type limit을 차례로 segment에 넣는다.
`objectKind`는 `actor`, `user_spot`, `instance_spot`, relocation policy는 `disabled`, `recreate`, `snapshot`
token을 사용한다. Snapshot 지원 여부는 `0` 또는 `1`이다. Actor capability의 Spot type limit은 빈
segment여야 한다. User·Instance Spot capability는 `0..2^31-1`의 invariant decimal segment를 사용하며
`0`은 별도 type limit이 없다는 뜻이다. Object Server는 Entry Spot ID presence가 `1`이고 Object Client와
object role `none`은 `0`이어야 한다. Entry Spot ID는 별도의 UUID byte representation으로 바꾸지 않고 full
Spot ID UTF-8 bytes를 digest에 넣는다.

ChannelName과 capability는 Unicode normalization을 수행하지 않고 unsigned UTF-8 byte lexical order로
정렬한다. `DescriptorRevision`, channel weight 값, node placement weight, maintenance wave, runtime state,
OwnerId, owner lease generation, update 시각, active·reserved 사용량과 activation active count는 digest에
포함하지 않는다. 이 값은 같은 lifecycle에서 변경할 수 있거나 owner admission fence가 별도로 검증하기
때문이다. 공식 fixture의 preimage와 digest를 모든 provider의 byte-level contract test에서 그대로 검증한다.

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
- `Found(Payload, StoreVersion, [ObjectGeneration](01-glossary.ko.md#objectgeneration), [AuthorityOwnerGeneration](01-glossary.ko.md#authorityownergeneration), OwnerId,
  OwnerLeaseGeneration, PlacementAllocation, StoreNow)`: current row snapshot을 반환한다.

Missing 결과에 `0`, 빈 문자열이나 synthetic StoreVersion을 넣지 않는다.

### 4.2 Expectation과 mutation

모든 public authority mutation은 Active `Found`의 expected StoreVersion을 명시한다. Missing→Reserved 생성은
generic Reserve가 전담한다. Redis Lua/function은 expectation을 먼저 검증한 뒤 global counter 소비, row write와
index write를 한 atomic operation으로 처리한다.

| Transition | 허용 expectation | generation 결과 |
|---|---|---|
| Preserve | Active Found | target owner token 없이 ObjectGeneration, AuthorityOwnerGeneration과 allocation 유지, 새 StoreRevision만 발급. Reserved standalone relocation fence가 있으면 같은 transaction에서 reservation의 expected StoreVersion을 새 StoreRevision으로 갱신 |
| NewOwner | Active Found | exact target owner lease·capacity fence 검증, target owner와 Active allocation 기록, ObjectGeneration 유지, 새 AuthorityOwnerGeneration과 StoreRevision 발급 |
| Delete | Active Found | 새 StoreRevision 발급, current active capacity 감소 뒤 row와 current index entry 제거 |

Authority payload에는 provider generation과 StoreVersion을 중복 encode하지 않는다. Wire fence는 provider metadata와
opaque StoreVersion을 Framework가 조합한다. Redis script는 payload body를 parse하거나 수정하지 않는다.
Put mutation은 Preserve에서 target owner token을 받지 않고 NewOwner에서 exact token을 반드시 받는다.
Redis script는 이 token의 owner lease row를 CAS와 같은 operation에서 확인하고 owner ID·lease generation을
authority metadata에 기록한다. Preserve와 Delete는 authority row에 저장된 current owner token의 lease를 같은
transaction에서 확인한다. Required lease가 missing·stale이면 current authority read를 담은 Conflict를 반환하고
row·index·counter를 변경하지 않는다. Token 존재 규칙을 위반한 mutation은 Redis operation 전에 argument
validation error로 거부한다. Relocation capacity fence는 NewOwner에서 반드시 있다. 일반 Preserve에는 없지만
standalone relocation의 `Captured` root 갱신과 `Prepared` 게시에는 같은 reserved fence가 있을 수 있다.
이 조합도 Redis operation 전에 검증한다.

Framework가 encode하는 authority payload는 relocation phase, `RelocationId`, immutable
source와 current target fence, Relocation Store root reference와 checksum, membership, replay·completion count를
포함한다. Redis provider는 이 field를 해석하지 않지만 expected StoreVersion CAS는 reference, checksum, phase,
membership과 count를 하나의 authority revision으로 바꾼다. Current owner ID·lease generation과 placement
allocation의 state·kind·stable type·descriptor key·lifecycle generation·typed capacity bundle은 provider metadata에만
두며 payload에 중복 encode하지 않는다. 일부 field만 별도 operation으로 갱신하지 않는다.

Logical create와 ObjectGeneration·initial allocation 발급은 아래 generic placement reservation operation만 사용한다.
Public authority CAS는 Missing row를 만들지 않는다.

Missing→Reserved는 `Reserve`, Reserved→Active는 exact `Commit`, application rejection의
Reserved→Missing은 exact `Reject`, infrastructure failure의 Reserved→Missing은 exact `Abort`만 수행한다.
Active→다른 Active는 capacity fence를 소비하는 NewOwner·aggregate commit, Active→Missing은 Delete만 수행한다.
Reserved row에 Preserve·NewOwner·Delete를 적용하면 Conflict이고 mutation은 0이다.

### 4.3 Generic placement reservation

Provider는 `Reserve`, `Commit`, `Reject`, `Abort` closed operation을 제공한다. Provider는 object
kind와 stable type을 placement allocation·capability·capacity counter를 선택하는 metadata로 처리하지만
Framework가 encode한 application·creation·relocation payload는 해석하지 않는다. Reservation은 object
kind, global canonical key, stable type, immutable creation intent reference·hash, target descriptor
key·lifecycle generation, typed capacity bundle, exact owner fence와 Framework가 encode한 opaque
Creating authority payload를 가진다. `Commit`은 Framework가 encode한 opaque Ready authority payload를
받는다. Redis provider는 두 payload를 해석하거나 합성하지 않고 해당 authority revision에 그대로 기록한다.
Reservation에는 TTL을 두지 않으며 Creating authority와 target host owner lease를 기준으로 recovery,
takeover 또는 abort한다.

| Operation | Atomic mutation |
|---|---|
| `Reserve` | `Missing → Creating`, generation 발급, creation intent 연결과 target reserved bundle 증가 |
| `Commit` | Target descriptor lifecycle·owner lease 재검증, exact `Creating → Ready`, reserved 감소와 active 증가, 해당 operation의 `Created` terminal publication |
| `Reject` | Exact Creating authority 삭제, reserved bundle 감소, 해당 operation의 `Rejected` terminal publication |
| `Abort` | Current lifecycle·lease와 무관하게 reservation에 고정한 exact Creating authority 삭제와 이전 target reserved bundle 감소 |

Expectation, global counter, authority row·index, reservation과 capacity counter는 같은 server-side
transaction에서 검증하고 변경한다. `Reserve`는 bundle 전체에 대해 Actor·Spot node limit과 Spot type
limit을 모두 확인한다. Actor의 `Commit`과 `Reject`는 reservation에 결합된 exact source operation
terminal만 publish한다. 다른 operation이 같은 Actor의 Creating authority를 기다리고 있으면 terminal
result를 읽지 않고 authority를 다시 조회한다. Ready면 `Existing`, Missing이면 새 reservation을 경쟁한다.

User Spot 또는 Instance Spot `Reserve`는 같은 Spot ID의 Entry claim key도 같은 transaction에서 확인한다.
`Claimed` record가 있으면 identity conflict로 닫고 authority row, generation, reservation, index와
capacity를 하나도 변경하지 않는다. Framework는 canonical Spot authority key와 Entry claim key를 모두
`KEYS`로 전달하며 provider는 RID 문자열에서 어느 key도 만들지 않는다.

`active + reserved + requested`가 limit을 넘으면 아무 값을 소비하지 않고
`PlacementCapacityExhausted`를 반환한다. `Commit`은 target descriptor lifecycle과 owner lease가
stale이면 mutation 0으로 끝내고 reservation을 유지한다. `Abort`는 stale lifecycle·lease를 이유로
거부하지 않고 reservation에 기록한 이전 descriptor·counter를 정리한다. 두 operation은 같은
reservation fence에 대해 idempotent하며 stale reservation이 새 authority나 capacity를 변경하지 못한다.

Creation request는 encoded 최대 1 MiB이고 immutable content reference와 hash로 저장한다. Ready 또는
fenced abort가 확정될 때까지 reference를 유지한다. Provider는 content를 해석하지 않는다. Recovery는
owner lease가 stale한 pending reservation을 exact fence로 takeover하거나 abort하며, elapsed time만으로
reservation을 삭제하지 않는다.

#### Relocation capacity reservation

Existing object relocation은 creation reservation을 재사용하지 않는다. `ReserveRelocationCapacity`는 Framework가
만든 non-zero 128-bit reservation ID, current authority key·StoreVersion, kind·stable type, source
descriptor key·lifecycle generation·owner token, target descriptor key·lifecycle generation·owner token과
typed capacity bundle을 받는다. Lua/function은 request source identity가 current authority owner와 durable Active
placement allocation의 descriptor key·lifecycle generation·kind·stable type·capacity bundle과 정확히 같은지
확인한다. Source descriptor row와 source owner lease의 live 상태는 요구하지 않는다. Target descriptor
lifecycle·owner lease·capability와 bundle의 모든 limit을 live/exact로 확인하고 target reserved bundle을 예약한다. Authority row와
source active count는 이 단계에서 바꾸지 않는다. 같은 ID와 exact request는 같은
fence를 반환하고 다른 request는 conflict다.

Fence가 있는 standalone `Preserve` CAS는 reservation의 authority key·expected StoreVersion·source·target owner와
durable allocation을 exact하게 검증한다. Lua/function은 opaque authority payload를 해석하지 않으며, authority
payload와 StoreRevision을 바꾸는 transaction 안에서 reservation JSON의 `expectedStoreVersion`만 새 revision으로
갱신한다. Owner·allocation·capacity counter와 fence의 `Reserved` 상태는 유지한다.

Standalone `NewOwner` CAS는 이렇게 재결합한 relocation capacity fence를 필수로 받아 authority owner 전환, source active 감소,
target reserved 감소·active 증가와 fence commit을 한 transaction에서 처리한다. Abort는 uncommitted
standalone fence의 target reserved bundle만 해제하며
반복 abort는 idempotent하고 committed 또는 다른 fence는 closed result로 구분한다. Reservation에는 TTL을 두지
않는다. Recovery는 expected authority StoreVersion, current owner token과 Active allocation을 사용하며 payload를 해석하지 않는다.
Standalone NewOwner fence가 reserved 상태가 아니거나 authority key·expected StoreVersion·source·target owner와
일치하지 않으면 current authority read를 담은 Conflict이며 authority row, capacity와 fence state의 mutation은
0이다. 이미 committed·aborted된 fence도 같다. CAS script는 request source와 durable Active allocation의 exact
match를 다시 확인하고 target descriptor lifecycle과 target owner lease만 live/exact로 재검증한다. Source
descriptor row·lease가 stale·missing이어도 allocation match가 유지되면 commit할 수 있다. Target이 stale이면
같은 Conflict와 mutation 0으로 끝낸다.
Aggregate prepare는 `OwnerTransition`에 따라 두 mode를 사용한다. `NewOwner`가 하나라도 있는 relocation mode는
`Preserve` participant를 함께 포함할 수 있지만 non-zero typed capacity bundle은 `NewOwner` participant의 durable
allocation delta만 exact 합산한다. User Spot initial relocation은 이 mode이며 `Actor=N, Spot=1, User Spot type=1`
중 실제 `NewOwner` participant에 해당하는 delta만 예약한다. 모든 participant가 `Preserve`인 completion·steady-
normalization mode는 exact zero capacity와 모든 empty membership mutation을 요구하고 capacity reservation 없이
authority payload만 atomic하게 바꾼다. 이때 owner, `ObjectGeneration`, `AuthorityOwnerGeneration`과 durable Active
allocation은 유지한다. Zero capacity와 `NewOwner`, non-zero capacity와 all-Preserve, bundle 또는 participant
inventory 불일치는 conflict이며 row·capacity·aggregate record를 변경하지 않는다.

### 4.4 Bounded aggregate commit

User Spot relocation과 cross-node Actor `JoinSpot`·`JoinEntrySpot`은 generic bounded aggregate transaction을 사용한다. Request는 non-zero
128-bit aggregate ID, exact aggregate generation, participant별 authority key·expected StoreVersion·owner mutation과
membership mutation을 가진다. Participant는 최대 1024개이고 encoded request와 aggregate record는 각각 최대
1 MiB다. Provider는 [User Spot](01-glossary.ko.md#entry-spot-user-spot과-instance-spot)이나 Actor 의미를 해석하지 않고 expectation과 mutation vector만 처리한다.

[Location Store](01-glossary.ko.md#location-store)의 aggregate record가 bounded canonical participant set, participant별 mutation, aggregate generation과
inventory digest를 권한 원본으로 저장한다. Relocation Store manifest는 participant별 state·journal payload를 찾기
위한 같은 digest의 projection일 뿐 authority가 아니다. Location Store transaction은 Relocation Store를 호출하거나
두 Store 사이 2PC를 수행하지 않는다.

`PrepareAggregate`는 모든 participant expectation을 확인하고 durable `Reserved` record를 만든다. Relocation
mode에서는 target owner lease fence를 확인하고 모든 `NewOwner` participant의 allocation delta와 정확히 같은
non-zero typed capacity bundle을 같은 transaction에서 한 번 예약한다. Standalone relocation capacity fence는
aggregate request에 사용하지 않는다. Completion·steady-normalization mode에서는 all-Preserve, exact zero
capacity와 empty membership mutation을 확인하고 capacity를 예약하지 않는다.
`CommitAggregate`는 record의 exact generation, canonical participant set과 inventory digest, source Active
allocation match와 target descriptor lifecycle·owner lease를 다시 확인한다. Source descriptor row·lease가
stale·missing이어도 allocation match가 유지되면 commit할 수 있다. Target이 stale이면 participant·capacity·
aggregate record를 바꾸지 않고 `Reserved`를 유지한다. 유효한 record만 소비해 모든 authority owner,
`AuthorityOwnerGeneration`, membership index와 aggregate commit generation을 한 server-side transaction으로
전환하고 record state를 `Committed`로 바꾼다. Completion·steady-normalization mode의 commit은 owner와 두
generation, membership과 durable Active allocation을 유지하고 participant payload만 atomic하게 변경한다.
`AbortAggregate`는 commit 전 `Reserved` record에서 relocation mode의 target reserved bundle만 정리하고
`Aborted`로 닫는다. 같은 aggregate generation의 duplicate operation은
idempotent하고 다른 generation은 stale다.

이 transaction은 Session [binding route](01-glossary.ko.md#binding-route)를 저장하거나 갱신하지 않는다. Actor가
Session에 bind되어 있으면 Framework runtime이 owner·membership commit,
callback·journal replay, durable source cleanup과 `Completed`를 끝낸 뒤 같은
ObjectGeneration을 검증하고 command 44·45로 Session owner가 보관한 해당 Actor의
binding route만 target owner로 갱신해 달라고 요청하고 확인을 받는다(`command 44·45`). Steady normalization 전에는
target session packet·push admission을 열지 않는다.

Expectation 하나라도 맞지 않으면 participant row, membership index, reservation, aggregate record와 counter를
변경하지 않는다. Commit 전에는 target owner나 membership 일부를 authority read와 index scan에 공개하지 않는다.
Commit 뒤에는 source participant 일부로 rollback하지 않으며 exact aggregate record를 사용해 target 전체 recovery를
계속한다. Recovery는 aggregate ID, generation, canonical participant set, typed capacity bundle과 current
owner lease를 모두 확인한다.

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
저장한다. Reserved full snapshot은 같은 13개 field와 네
`R:pendingCreation*` field를 모두 저장하므로 current row와 같은 17개 field를
복원한다. Reserved snapshot에서 네 field가 하나라도 빠지거나 Active snapshot에
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
weight와 current active·reserved count는 더 큰 revision으로 갱신할 수 있다. Channel weight를 placement weight로
합성하지 않는다. Reservation transaction이 변경한 count와 descriptor projection이 같은 값을 나타내야 하며,
projection lag를 capacity 권한으로 사용하지 않는다.

RouteMesh Channel, ClientServer Server와 node-wide placement weight는 signed decimal integer
`0..10000`으로 encode한다. Provider는 범위 밖 값이나 정수가 아닌 값을 저장하지 않고 configuration error를
반환한다. Weight update는 같은 lifecycle의 더 큰 descriptor revision과 함께 atomic하게 게시한다. Lowe
revision, 같은 revision의 다른 값과 narrow unsigned representation으로 truncate한 값은 거부한다.

## 7. Routing ID descriptor owner CAS

Automatic RID의 uniqueness는 descriptor owner CAS가 `(MeshName, RID)` active conflict를 atomic하게 확인해
보장한다. RID는 Framework가 `prefix-<lowercase-canonical-uuid-v4>` 형식으로 생성하며 Redis provider는 RID
구조나 prefix를 해석하지 않는다. Claim은 exact host owner lease token과 descriptor identity를 함께
기록하고, renew·update·release는 같은 token을 비교한다.

Active conflict는 기존 descriptor를 덮어쓰지 않는다. Framework는 새 UUID나 두 번째 claim을 만들지 않고
`RoutingIdConflict`로 startup을 즉시 실패한다. Replacement lifecycle은 startup 전에
새 UUID v4 RID를 발급하지만, 그 lifecycle의 claim이 충돌해도 다시 생성하지 않고
실패한다.

### 7.1 Entry Spot global identity claim

Object Server lifecycle의 Entry Spot ID는 MeshNode RID와 별도로 생성한 RFC 4122
UUID v4 component를 사용한다. UUID는 lowercase canonical `8-4-4-4-12` 형식이다.
Framework는 provider를 호출하기 전에 UUID 형식·version·variant와 전체 Spot ID의
UTF-8 길이를 검증한다. Redis provider는 전체 Spot ID를 opaque bytes로 취급하며
prefix, marker와 UUID를 해석하지 않는다.

Object Server MeshNode descriptor의 새 claim은 별도 public Entry claim method를
호출하지 않는다. 같은 server-side transaction에서 다음 항목을 모두 확인하고
기록한다.

1. 현재 host owner lease와 descriptor lifecycle이 정확히 일치하는지 확인한다.
2. 같은 `(MeshName, MeshNode RID)`를 사용 중인 descriptor가 있는지 확인한다.
3. 같은 Entry Spot ID를 claim한 record나 User·Instance Spot authority가 있는지
   확인한다.
4. Public descriptor, admission metadata, descriptor index·owner index와 Entry
   claim을 함께 생성한다.

RID나 Entry Spot ID가 충돌하면 descriptor, claim, index와 counter를 하나도
변경하지 않는다. 같은 descriptor bytes, lifecycle, owner token과 Entry claim
field를 다시 전달한 exact duplicate만 idempotent하게 성공한다. 다른 active
claim과 충돌하면 `RoutingIdConflict`로 끝내며 Framework는 다른 Entry Spot ID로
재시도하지 않는다.

Framework는 descriptor key, Entry claim key와 같은 Spot ID의 authority key를
모두 Redis script의 `KEYS`로 전달한다. Provider가 descriptor JSON이나 RID를
해석하여 key를 만들지 않는다. Descriptor를 제거할 때는 저장된 owner lease,
lifecycle과 연결된 Entry claim을 함께 검증한 뒤 descriptor, index와 claim을 같은
transaction에서 삭제한다. 이전 lifecycle의 cleanup은 successor lifecycle이
claim한 descriptor와 Entry claim을 변경할 수 없다.

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
- Reserve가 exact target owner lease를 검증하고 initial generation·Reserved allocation·row/index를 한
  operation으로 만들며 NewOwner가 exact target owner lease와 relocation capacity fence를 검증하고 Active
  allocation·owner metadata를 한 operation으로 교체한다.
- 모든 generation counter가 `2^63-1`에서 `GenerationExhausted`를 stable하게 반환하고 아무 값도 소비하지 않는다.
- Authority row에 TTL이 없고 host owner lease 만료만으로 row가 삭제되지 않는다.
- `RemoveAllByOwner`가 exact host token의 ephemeral descriptor만 제거하고 durable authority·reservation은 유지한다.
- Authority scan이 1000 item·4 MiB·4096-byte opaque cursor와 snapshot consistency를 지킨다.
- Descriptor page가 1000 item·4 MiB를 지키고 unstable scope stamp 결과를 적용하지 않는다.
- Oversize descriptor와 capability vector가 startup을 실패시키며 partial descriptor를 publish하지 않는다.
- Descriptor owner CAS가 active RID conflict를 덮어쓰지 않으며 exact host lease fence를 사용한다.
- Object Server descriptor `NewClaim`이 descriptor와 Entry Spot ID claim을 한 transaction에서 만들고,
  existing Entry claim 또는 같은 RID의 User·Instance Spot authority와 충돌하면 partial write를 만들지 않는다.
- User·Instance Spot `Reserve`가 같은 RID의 active Entry claim을 확인하고 generation·authority·capacity를
  소비하지 않은 채 identity conflict로 끝난다.
- Descriptor remove와 `RemoveAllByOwner`가 exact owner lease·lifecycle·claim field를 확인해 stale
  cleanup으로 successor Entry claim을 삭제하지 않는다.
- Automatic MeshNode RID와 Entry Spot ID claim이 active conflict에서 mutation 0과
  `RoutingIdConflict`로 즉시 끝나며 다른 UUID로 retry하지 않는다.
- Entry Spot ID의 UUID component가 RFC 4122 UUID v4 lowercase canonical 형식이고 provider가 full
  Spot ID를 parse하지 않는다.
- Entry Spot ID가 descriptor immutable digest와 update fence에 포함되어 mutable update로 바뀌지 않는다.
- `Reserve`, `Commit`, `Abort`가 authority와 Actor·Spot·Spot type capacity를 atomic하게 전환한다.
- Found·Stored·scan snapshot이 provider-owned Reserved·Active allocation metadata를 완전하게 반환한다.
- Relocation reserve·standalone commit·aggregate commit이 request source를 durable Active allocation에
  exact-match하고 source descriptor·lease stale recovery는 허용하되 target stale commit은 no-write로 막는다.
- Standalone `Captured` root 갱신과 `Prepared` Preserve가 reserved fence의 expected StoreVersion을 authority
  CAS 결과와 같은 transaction에서 재결합하고 capacity와 fence 상태는 바꾸지 않는다.
- Aggregate relocation prepare가 `NewOwner` participant의 non-zero typed bundle만 한 transaction에서 예약하고
  mixed `Preserve` participant의 allocation을 유지한다.
- Aggregate completion·steady-normalization prepare가 all-Preserve, zero capacity와 empty membership mutation만
  허용하고 owner·generation·Active allocation을 유지한 payload-only atomic CAS를 수행한다.
- Zero capacity와 `NewOwner`, non-zero capacity와 all-Preserve 조합을 conflict와 mutation 0으로 거부한다.
- Delete가 live current owner lease와 Active allocation을 검증하고 exact active capacity bundle을 row와 함께
  atomic하게 제거한다.
- Reserved reservation recovery가 elapsed time이 아니라 exact owner lease와 authority fence를 사용한다.
- Creation request reference와 hash가 Ready 또는 fenced abort까지 유지되고 encoded 1 MiB bound를 지킨다.
- Reserved authority의 네 `pendingCreation*` field와 history의 17개 field가
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
