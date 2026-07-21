# Redis Location Store — 공통 스펙

[스펙 목차](../README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위

이 문서는 Framework 11.0 Redis provider가 descriptor, host owner lease, durable object authority, routing ID allocation과
checkpoint를 저장하는 규칙을 정의한다. Provider는 authority payload와 checkpoint payload를 해석하지 않는다.
Framework가 schema에 따라 bytes를 encode·decode하고 Redis는 key, generation, StoreVersion, TTL과 atomic CAS만
관리한다.

Redis server time이 lease와 checkpoint 만료의 기준이다. Application host의 wall clock은 authority 판단에
사용하지 않는다.

## 2. 저장 영역과 수명

Redis key prefix는 배포 단위에서 설정할 수 있지만 같은 provider transaction domain에서는 다음 논리 영역을
분리한다.

| 영역 | 값 | 수명 |
|---|---|---|
| Descriptor | MeshNode, ClientServer server, fanout publisher descriptor와 host owner lease token | host lease에 종속된 ephemeral data |
| Host owner lease | `(OwnerId, LeaseGeneration, ExpiresAt, StoreNow)` | Redis TTL |
| Object authority | canonical authority key, opaque payload, StoreVersion, ObjectGeneration, AuthorityOwnerGeneration | 명시적 fenced delete까지 durable, TTL 금지 |
| Authority index | snapshot scan용 versioned key index와 active scan lease | authority row와 scan lease에 종속 |
| Routing allocation | group configuration, slot assignment와 host owner lease token | exact host token에 종속 |
| Checkpoint | immutable data chunk와 root manifest | 24시간 renewable TTL |
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

한 Framework host process lifecycle은 owner lease token 하나를 claim한다. 같은 host의 모든 descriptor, routing
allocation과 object authority가 이 token을 공유한다. Token은 Framework가 만든 재사용 불가능한 OwnerId와
provider가 global counter에서 발급한 non-zero LeaseGeneration의 조합이다.

Provider는 다음 exact operation만 제공한다.

- Claim과 expired-row Takeover는 OwnerId와 TTL을 받아 새 token, ExpiresAt과 StoreNow를 반환하거나
  conflict 또는 `GenerationExhausted`로 끝난다. `GenerationExhausted`는 새 LeaseGeneration이 필요한
  경우에만 가능하며 row, index와 counter를 변경하거나 소비하지 않는다.
- Read는 OwnerId를 exact key로 읽어 current token, ExpiresAt과 StoreNow를 반환하거나 Missing으로 끝난다.
- Renew는 exact token과 TTL을 비교해 같은 token의 expiry만 갱신하거나 Stale로 끝난다.
- Release는 exact token을 비교해 lease와 ephemeral owner index를 제거하거나 Stale로 끝난다.

Renew와 Release는 새 LeaseGeneration을 발급하지 않으므로 `GenerationExhausted`를 반환하지 않는다.

Provider-wide `ListOwnerLeases`는 제공하지 않는다. Routing, resolve와 transfer admission은 항상 필요한 OwnerId를
exact Read한다. Lease 목록은 권한 판단 근거가 아니다.

`RemoveAllByOwner`는 `(OwnerId, LeaseGeneration)` exact token으로 index된 descriptor와 routing allocation만
제거한다. TTL-free object authority는 제거하지 않는다. Authority 삭제는 expected StoreVersion과 current owner
fence를 받는 별도 explicit CAS delete만 허용한다.

## 4. Durable authority CAS

### 4.1 Canonical key와 read

Framework는 `service-wire-v1.schema.json`의 `authority-key-v1` 규칙으로 canonical key를 만든다. Redis provider는
key bytes를 opaque 값으로 취급한다. Direct resolve는 exact key read만 사용한다.

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

Instance cold activation은 source coordinator가 target transport에 제출하기 전에 `NewObject` CAS를 수행한다.
이 CAS가 non-zero ObjectGeneration과 AuthorityOwnerGeneration을 함께 발급한다. Target은 authority를 claim하지 않고
exact owner lease와 authority를 다시 확인한 뒤 factory, activation barrier와 Ready CAS만 수행한다.

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

Redis `SCAN`, 모든 row를 한 번에 복사하는 Lua와 unbounded materialization은 금지한다. Startup은 등록된 MeshName
scope의 complete snapshot scan을 끝낸 뒤 stateful serving을 연다. Background recovery도 같은 page 계약을 사용한다.

## 6. Descriptor enumeration

Descriptor list는 page size 1..1000과 encoded page 최대 4 MiB를 지킨다. Framework는 scope change stamp를 page
열거 전후에 읽고 두 값이 같을 때만 전체 page 결과를 desired set으로 적용한다. 값이 다르면 결과를 버리고
bounded retry한다. Continuation은 provider-issued opaque cursor이며 provider가 unbounded list를 먼저 만들면 안 된다.

Host는 startup에서 모든 Channel, type capability와 readable state contract를 포함한 complete descriptor를 먼저
만든다. Encoded descriptor는 최대 1 MiB, type/stateful capability vector는 각각 최대 1024개, type별 readable
state-contract set은 최대 1024개다. 하나라도 넘으면 configuration/startup을 atomic하게 실패한다. Descriptor를
truncate, split하거나 일부만 publish하지 않는다.

`update`는 current admitted physical connection에만 적용한다. Topology, identity, endpoint connection identity,
RID, lifecycle generation, normalized max message size, channel membership key, capability와 application version은
immutable하다. Existing channel weight, runtime state, capacity와 maintenance wave만 더 큰 revision으로 갱신한다.
같은 revision·같은 bytes는 idempotent이고 lower revision은 stale다. 같은 revision의 다른 bytes나 immutable 변경은
protocol error이며 connection을 not-ready로 바꾼다.

DescriptorRevision은 Framework caller가 descriptor마다 발급하며 Redis provider counter가 아니다. 값이
`2^63-1`에 도달해 다음 revision이 필요하면 Framework host는 wrap하지 않고 `Error`로 seal하며
publish를 시도하지 않는다.

## 7. Routing ID allocation

Allocation group snapshot은 slot count 1..65535와 member count 1..255의 coherent value다. Group configuration과
slot assignment를 같은 atomic script에서 비교한다. Page로 나누거나 부분 group을 publish하지 않는다. Assignment는
host owner lease token을 저장하며 renew와 release는 exact token을 비교한다.

Routing allocation은 별도 owner lease를 만들지 않는다. 같은 host lifecycle token과 local monotonic owner lease
deadline을 사용한다.

## 8. Checkpoint chunk와 manifest

Checkpoint logical stream은 최대 256 GiB다. Provider는 최대 64 MiB data를 가진 immutable chunk와 최대 4096개
chunk reference를 가진 immutable root manifest를 저장한다. Manifest에는 logical version, total length, total
CRC32C와 ordered `(reference, length, CRC32C)`가 포함된다. Provider는 chunk, manifest와 application JSON을
해석하지 않는다.

Write 순서는 모든 chunk, root manifest, authority reference CAS다. Authority가 참조하지 않은 chunk와 manifest는
orphan이며 복구 authority가 아니다. 각 tree component의 retention은 24시간이고 renew threshold는 12시간이다.
Framework는 아직 authority에 연결되지 않은 staged component도 provider ExpiresAt과 StoreNow로 추적한다.
`Captured`와 `Prepared` CAS 직전에 complete tree의 모든 component가 12시간보다 긴 remaining lifetime을 갖는지
검증하고 필요하면 tree 전체를 renew한다. Missing 또는 partial renew는 precommit abort이며 root를 authority에
연결하지 않는다.

Authority가 가리키는 tree의 renew와 delete는 current authority key, StoreVersion과 root reference를 exact 확인한
뒤 수행한다. Partial renew는 성공으로 처리하지 않는다. Completion append, `replyRelayAck` 또는 exact request-source
owner lease expiry를 기록할 때는 새 immutable root를 먼저 만든 뒤 expected StoreVersion CAS 한 번으로 root,
checksum, TerminalCompletionCount와 PendingRelayCount를 함께 교체한다. Conflict loser의 root는 orphan으로 남겨
expiry 또는 idempotent delete로 정리한다.

## 9. Store 장애와 recovery

`StoreFailureGrace`는 descriptor discovery reconcile과 새 outbound connect에만 적용한다. 마지막 stable desired set은
grace 동안 유지할 수 있고 existing transport는 service liveness를 계속 적용한다. Grace가 끝난 뒤 stable page
snapshot을 다시 얻기 전에는 새 connection을 만들지 않는다.

Grace는 host owner lease, coordinator lease와 local authority deadline을 연장하지 않는다. 마지막 valid owner lease
read에서 계산한 monotonic deadline에 도달하면 Actor·Spot·Instance message, timer, factory completion, transfer
source·target·coordinator CAS와 reservation admission을 seal한다. Store 복구 뒤 exact owner token과 stable page set을
다시 검증한 다음 diff와 new connect를 적용한다.

Recovery는 authority scan item을 exact Read한 뒤 expected StoreVersion CAS로만 변경한다. Scan item payload,
descriptor snapshot 또는 expired owner ID만으로 owner를 바꾸지 않는다.

## 10. Atomicity와 오류

Redis implementation은 row, global counter, index와 Redis `TIME`을 읽고 쓰는 operation을 하나의 Lua script 또는
동등한 server-side atomic function으로 구현한다. Cluster deployment는 한 transaction domain의 관련 key가 같은
hash slot에 배치되도록 prefix/hash tag를 구성한다. 이를 보장할 수 없으면 provider startup을 실패한다.

Script timeout, failover와 connection loss로 commit 여부가 불명확하면 Framework는 exact Read로 결과를 확인한다.
같은 expectation을 임의로 새 mutation처럼 재실행하지 않는다. Opaque payload decode failure, counter overflow,
authority/checkpoint count mismatch와 missing referenced checkpoint는 recovery error이며 Ready/Completed를 publish하지
않는다.

Provider operation을 시작하기 전 cancellation은 I/O와 commit을 모두 막을 수 있다. Operation을 시작한 뒤 waiter
cancellation, timeout 또는 provider error는 commit 실패를 뜻하지 않으며 결과가 불명확하다. Authority CAS는 exact
key와 expected fence를 다시 읽어 reconcile한 뒤에만 retry한다. Content-addressed checkpoint Put은 같은 bytes를
verify하거나 idempotent하게 retry하고, authority에 연결되지 않은 committed Put은 orphan retention과 cleanup으로
처리한다. Renew와 delete도 idempotent다.

Framework가 provider에 넘긴 key와 value bytes는 async operation 완료까지 변경되지 않고 유효해야 한다. Provider가
그 이후 bytes를 보관하려면 복사한다. Provider success result의 bytes는 immutable하고 호출자가 보관할 수 있어야
한다. Mutable Redis adapter buffer를 사용하면 provider가 반환 전에 defensive snapshot을 만든다.

## 11. 검증 요구

- Missing read가 StoreNow만 반환하고 synthetic StoreVersion과 generation을 만들지 않는다.
- NewObject와 NewOwner가 global counter와 row/index를 한 operation으로 변경한다.
- 모든 generation counter가 `2^63-1`에서 `GenerationExhausted`를 stable하게 반환하고 아무 값도 소비하지 않는다.
- Authority row에 TTL이 없고 host owner lease 만료만으로 row가 삭제되지 않는다.
- `RemoveAllByOwner`가 exact host token의 ephemeral descriptor·allocation만 제거한다.
- Authority scan이 1000 item·4 MiB·4096-byte opaque cursor와 snapshot consistency를 지킨다.
- Descriptor page가 1000 item·4 MiB를 지키고 unstable scope stamp 결과를 적용하지 않는다.
- Oversize descriptor와 capability vector가 startup을 실패시키며 partial descriptor를 publish하지 않는다.
- Routing allocation group이 coherent snapshot과 exact host lease fence를 사용한다.
- Long capture 중 staged checkpoint를 renew하고 pre-link partial renew failure가 authority reference를 만들지 않는다.
- Completion root와 authority count가 한 CAS로 교체되고 mismatch가 Completed를 막는다.
- StoreFailureGrace가 discovery만 freeze하고 authority deadline을 연장하지 않는다.
- Redis failover 뒤 exact read가 uncertain CAS의 실제 결과를 결정한다.
- Commit 성공 뒤 response loss·waiter cancellation이 rollback으로 오인되지 않고 exact read 또는 idempotent Put으로
  reconcile된다.
- Async provider operation 동안 input bytes가 유지되고 result bytes가 immutable snapshot이다.
