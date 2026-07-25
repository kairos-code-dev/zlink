# Redis Relocation Store

[공통 스펙 목차](README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Redis Location Store](41-location-store-redis.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 이 문서가 정의하는 범위

이 문서는 Framework 11.0 Redis Relocation Store가 cross-node Actor·Spot 이동과,
실행 중인 Instance Spot이 없을 때 새 [Spot](01-glossary.ko.md#spot)을 준비하고 최초 message를 복구 가능하게
보관하는 과정인 `cold activation`에 필요한 immutable payload를 저장하는 규칙을
정의한다.

Relocation Store에는 다음 정보가 속한다.

- Application state
- Seal 시점에 아직 실행하지 않은 message queue
- Accepted journal
- Timer logical registration과 pending tick
- Participant payload
- Manifest와 Framework metadata
- Cross-node Accepted completion의 `OperationId`, optional reply와 retry cursor
- Replay와 recovery payload
- Complete activation envelope
- Durable activation inbox의 첫 record

쉽게 말해 Location Store가 “어느 node가 새 owner인지”를 기록한다면 Relocation
Store는 “새 [owner](01-glossary.ko.md#owner)가 실행을 이어가기 위해 읽어야 할 state와 미처리 작업”을
저장한다. Owner와 membership을 결정하는 권한은 [Location Store](01-glossary.ko.md#location-store)에 있으며 Relocation
Store의 payload만으로 owner를 바꾸지 않는다.

Provider는 relocation·[activation envelope](01-glossary.ko.md#activation-envelope), application state, message, timer와 journal record의
내용을 해석하지 않는다.

Relocation Store는 Session binding route, 즉 Session owner가 현재 Actor owner에
전달할 때 사용하는 경로도 저장하거나 갱신하지 않는다. Actor가
Session에 bind되어 있으면 Framework runtime이 Location Store의 owner·membership
commit, callback·journal replay, durable source cleanup과 `Completed`를 끝낸 뒤 같은
ObjectGeneration을 검증하고 command 44·45로 Session owner가 보관한 해당 Actor
route만 target owner로 갱신해 달라고 요청하고 확인을 받는다(`command 44·45`). Steady normalization 전에는 target
Actor의 session packet·push admission을 열지 않는다. 같은 Session의 다른 Actor
route와 physical STREAM connection은 유지한다. Route 갱신은 같은 `ObjectGeneration`에만
적용하며, 새 incarnation은 application이 명시적으로 다시 bind해야 한다.

Relocation Store는 Location Store와 별도 public interface, 별도 등록과 별도 Redis implementation을 사용한다.
두 implementation은 같은 Redis deployment 또는 cluster를 서로 다른 key prefix로 사용할 수 있고 물리적으로
분리된 Redis를 사용할 수도 있다. Location Store transaction은 Relocation Store key를 포함하지 않으며 두 Store
사이에 distributed transaction이나 2PC를 요구하지 않는다.

## 2. Immutable root와 manifest

한 번의 relocation이 저장하는 전체 logical stream은 aggregate와 accepted
journal을 합쳐 최대 256 GiB다. Relocation adapter 하나가 반환하는 application
state는 최대 64 MiB다.

Provider는 큰 payload를 최대 64 MiB 단위의 변경할 수 없는 chunk로 나누고, 최대
4096개 chunk reference를 하나의 root manifest에 순서대로 기록한다. Manifest에는
logical version, 전체 길이, 전체 CRC32C와 각 chunk의 `(reference, length,
CRC32C)`가 들어간다. 저장한 payload와 manifest를 직접 수정하지 않으며 completion을
추가할 때도 새 root를 만든다.

Process의 encoded payload in-flight 기본 상한 256 MiB는 Framework coordinator gate이며 Store object 크기나 logical
stream ceiling이 아니다. Framework는 source queue를 seal하기 전에 Snapshot participant마다 64 MiB와 이미
Framework가 소유한 section의 deterministic encoded upper bound를 합한 byte permit을 얻는다. `Capture` 뒤에는 actual
encoded size로 permit을 축소만 한다. 한 User Spot aggregate의 reservation이 gate보다 크면 다른 payload가
in-flight가 아닌 동안에만 exclusive oversized aggregate 하나로 저장·복원한다. Standalone Actor와 [Instance Spot](01-glossary.ko.md#entry-spot-user-spot과-instance-spot)
unit은 gate 안에서만 admit한다. Permit attempt는 all-or-nothing이며 실패한 unit은 일부 permit을 보유한 채
기다리지 않는다.

User Spot aggregate와 maintenance inventory의 권한 원본은 Location Store의 bounded canonical participant set이다.
Relocation manifest는 participant별 state·journal payload를 찾기 위한 projection과 같은 canonical inventory digest를
가질 수 있지만 owner·[membership](01-glossary.ko.md#membership) commit의 권한 근거가 아니다. Runtime은 Location participant set의 digest와
Relocation manifest digest가 정확히 같은 경우에만 restore와 replay를 시작한다.

Actor Join의 public `OperationId`는 completion callback의 중복 처리를 막는 별도
field다. `RelocationId`, placement reservation ID나 aggregate commit ID를
`OperationId`로 대신 사용하지 않는다. Same-node Join, `Rejected`와 commit 전
`Failed`는 process 재시작 뒤 completion replay를 보장하지 않으므로 이 목적의
durable manifest를 만들지 않는다. Location Store의 bounded aggregate commit까지
성공한 cross-node `Accepted`만 manifest의 `OperationId`, optional reply와 cursor를
사용해 completion을 at-least-once로 복구한다.

각 tree component의 retention은 24시간이고 renew threshold는 12시간이다. Provider는 Redis server time으로
`ExpiresAt`과 `StoreNow`를 계산한다. Application host wall clock은 retention 판단에 사용하지 않는다.

## 3. Provider operation

Relocation Store는 opaque payload를 저장하고 읽고, retention을 갱신하고, reference를 삭제하는 generic operation만
제공한다. Actor·Spot별 Store interface, relocation phase별 method와 Redis 전용 root 등록 API는 제공하지 않는다.

- Put은 content-addressed reference, checksum, `ExpiresAt`과 `StoreNow`를 반환한다. 같은 content의 uncertain
  completion은 stored bytes를 verify한 뒤 idempotent하게 retry한다.
- Read는 exact reference의 immutable bytes 또는 `Missing` closed result를 반환한다.
- Renew는 complete tree의 retention을 갱신한다. 존재하지 않는 reference는 정상
  `Missing` 결과다. 일부 component만 갱신되면 성공이 아니며 runtime은 root를
  authority에 연결하지 않는다.
- Delete는 exact reference를 idempotent하게 제거한다. 존재하지 않는 reference는 정상 `Missing` 결과다.

Framework가 provider에 넘긴 input bytes는 asynchronous operation이 끝날 때까지 유효하고 변경되지 않는다.
Provider가 그 이후 bytes를 보관하려면 복사한다. Success result bytes는 consumer가 사용하는 동안 immutable하고
stable해야 한다.

## 4. Location authority와 연결 순서

Cross-store 저장 순서는 다음과 같다.

1. Relocation Store에 모든 immutable chunk를 저장한다.
2. Root manifest를 저장하고 reference, checksum, canonical inventory digest와 retention을 검증한다.
3. `Captured` 또는 root replacement [authority](01-glossary.ko.md#authority) CAS가 Location Store에 reference, checksum, phase와 count를 연결한다.
4. CAS에 연결되지 않은 root와 chunk는 orphan retention 또는 idempotent cleanup으로 제거한다.

`Captured`와 `Prepared` CAS 직전에 complete tree의 모든 component가 renew threshold보다 긴 remaining lifetime을
갖는지 검증하고 필요하면 tree 전체를 renew한다. Missing 또는 partial renew는 precommit abort이며 Location
authority에 reference를 연결하지 않는다.

Completion append, `replyRelayAck` 또는 exact request-source owner lease expiry를 기록할 때는 새 immutable root를
먼저 만든다. 그 뒤 Location Store expected-version CAS 한 번으로 root reference, checksum,
`TerminalCompletionCount`와 `PendingRelayCount`를 함께 교체한다. Conflict loser의 새 root는 orphan이다.

### 4.1 Instance Spot cold activation 저장 순서

Instance Spot cold activation도 Location Store가 reference를 공개하기 전에 Relocation
Store의 payload를 먼저 확정한다.

1. Target은 operation identity, send/request 구분, source node RID와 lifecycle
   generation, optional source Spot ID, reply correlation, deadline, target descriptor
   fence, command 39의 optional metadata 존재 여부와 metadata frame, application
   payload를 포함한 complete activation envelope를 변경할 수 없는 root로 저장한다.
2. Reference, SHA-256, encoded size와 retention을 확인한다. 그 뒤 Location Store의
   `Reserve`가 recovery receipt를 `Creating` authority와 Pending creation projection에
   한 transaction으로 연결한다.
3. Factory와 initialize가 끝나면 Framework는 root의 first message를 durable
   activation inbox의 첫 record로 확정한다. 이때까지 handler 실행은 activation
   barrier로 차단한다.
4. 이 root와 durable inbox 규칙은 target이 owner claim을 획득하여 만드는
   target-owned Instance [cold activation](01-glossary.ko.md#cold-activation)에만 적용한다. `Ready` commit은 recovery
   root와 replay cursor를 authority payload에 유지한다.
   Runtime은 first record를 local queue 선두에 복원한 뒤 barrier를 연다. 최초
   handler의 완료를 durable하게 기록하고 [replay cursor](01-glossary.ko.md#replay-cursor)를 inbox sequence까지 갱신한
   뒤에만 `Preserve` CAS로 recovery pointer를 제거한다. Queue에 넣었다는 사실만으로
   pointer를 제거하지 않는다.
5. Activation recovery pointer는 `Ready` Instance cold activation에만 존재한다.
   Actor, Entry Spot, User Spot과 `Creating`·`Closing`·`Relocating` authority에는
   둘 수 없다.
6. Pointer 제거가 성공한 뒤 root를 삭제한다. `Reserve` 전에 실패한 root와
   `Reserve` conflict에서 진 target의 root는 orphan이다.

Instance [factory](01-glossary.ko.md#factory)의 relocation policy가 `Disabled`여도 cold activation에는 Relocation
Store를 사용한다. Instance Spot factory를 하나라도 등록한 Object Server는 Relocation
Store를 정확히 하나 등록해야 한다.

## 5. Reference release와 data loss

Target restore와 recovery는 current Location authority가 연결한 exact reference만 사용한다. Relocation manifest만으로
owner, membership, phase나 recovery target을 결정하지 않는다.

Runtime은 source cleanup, accepted request의 terminal completion, reply relay ACK 또는 source lease expiry와 steady
authority normalization을 모두 확인한 뒤 Location authority에서 reference를 먼저 release한다. Reference release
CAS가 성공하기 전에는 Relocation root를 삭제하지 않는다. Release 뒤에는 즉시 idempotent delete하거나 recovery
retention까지 유지할 수 있다.

Authority가 publish한 reference의 root가 일시적으로 보이지 않으면 bounded retry와 exact re-read를 수행한다.
Provider가 영구 `Missing`을 확정하거나 checksum·inventory digest가 일치하지 않으면 non-retriable
`RelocationDataLost`로 seal한다. Commit된 owner와 membership을 source로 rollback하거나 이전 root를 추측해 복원하지
않는다.

## 6. 공식 Redis extension

공식 Redis extension package는 Location Store와 Relocation Store를 서로 다른 class로 제공한다. 각 instance는
자신의 connection 설정과 비어 있지 않은 key prefix를 가진다. 한 class가 두 Store interface를 함께 구현하거나
두 capability를 한 번에 root에 등록하는 Redis 전용 API는 제공하지 않는다.

같은 Redis deployment를 사용할 때도 prefix가 겹치면 startup을 실패한다. 별도 deployment를 사용할 때 Location
authority의 availability와 Relocation payload의 availability는 독립적이며 한쪽 Redis script가 다른 Store의 key를
읽거나 변경하지 않는다.

## 7. 구현 및 contract test 검증 요구

- Chunk와 root가 immutable하고 completion append가 새 root를 만든다.
- Relocation manifest의 inventory digest가 Location Store의 authoritative canonical participant set과 일치한다.
- Cross-node Actor Join의 `Accepted` manifest가 public completion `OperationId`,
  optional reply와 retry cursor를 서로 다른 field로 보존한다.
- Public completion
  [Actor Join `OperationId`](01-glossary.ko.md#actor-join-operationid)가
  `RelocationId`, reservation ID나 aggregate
  commit ID를 재사용하지 않는다.
- Same-node Join, `Rejected`와 commit 전 `Failed`를 process 재시작 뒤 replay하기
  위한 새 durable record를 만들지 않는다.
- Put과 root 검증이 authority CAS보다 먼저 수행된다.
- CAS conflict로 연결되지 않은 root가 orphan retention 또는 idempotent cleanup으로 제거된다.
- Location authority reference release가 Relocation root delete보다 먼저 수행된다.
- `Recreate`가 application state 없이도 accepted journal과 recovery payload를 Relocation Store에 기록한다.
- Published root의 permanent missing·checksum mismatch·inventory digest mismatch가 `RelocationDataLost`이며 rollback하지
  않는다.
- Instance activation root가 complete envelope를 보존하고 Pending authority exact
  read가 [recovery receipt](01-glossary.ko.md#recovery-receipt)와 provider가 발급한 reservation fence를 복원한다.
- [Durable activation inbox](01-glossary.ko.md#durable-activation-inbox)의 첫 record를 `Ready` commit 전에 확정하고, startup
  Serving gate는 queue 선두 복원이 끝난 뒤에만 연다.
- Location과 Relocation Redis가 같은 deployment와 분리 deployment에서 모두 동작하고 cross-store transaction을
  요구하지 않는다.
