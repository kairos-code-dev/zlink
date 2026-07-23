# Redis Relocation Store — 공통 스펙

[스펙 목차](../README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Redis Location Store](41-location-store-redis.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위

이 문서는 Framework 11.0 Redis Relocation Store가 cross-node Actor·Spot 이동에 필요한 immutable payload를
저장하는 규칙을 정의한다. Application state, seal 시점에 실행하지 않은 message queue, accepted journal,
timer logical registration·pending tick, participant payload, manifest·Framework metadata, terminal completion과
replay·recovery payload가 이 Store에 속한다. Provider는 relocation envelope, application state, message·timer와
journal record를 해석하지 않는다.

Relocation Store는 Location Store와 별도 public interface, 별도 등록과 별도 Redis implementation을 사용한다.
두 implementation은 같은 Redis deployment 또는 cluster를 서로 다른 key prefix로 사용할 수 있고 물리적으로
분리된 Redis를 사용할 수도 있다. Location Store transaction은 Relocation Store key를 포함하지 않으며 두 Store
사이에 distributed transaction이나 2PC를 요구하지 않는다.

## 2. Immutable root와 manifest

Relocation logical stream은 aggregate 전체와 accepted journal을 포함해 최대 256 GiB다. Relocation adapter 하나가
반환하는 application state는 최대 64 MiB다. Provider는 최대 64 MiB data를 가진 immutable chunk와 최대 4096개
chunk reference를 가진 immutable root manifest를 저장한다. Manifest에는 logical version, total length, total
CRC32C와 ordered `(reference, length, CRC32C)`를 포함한다. Payload bytes와 manifest content는 저장 뒤 변경하지
않으며 completion을 추가할 때도 새 root를 만든다.

Process의 encoded payload in-flight 기본 상한 256 MiB는 Framework coordinator gate이며 Store object 크기나 logical
stream ceiling이 아니다. Framework는 source queue를 seal하기 전에 Snapshot participant마다 64 MiB와 이미
Framework가 소유한 section의 deterministic encoded upper bound를 합한 byte permit을 얻는다. `Capture` 뒤에는 actual
encoded size로 permit을 축소만 한다. 한 User Spot aggregate의 reservation이 gate보다 크면 다른 payload가
in-flight가 아닌 동안에만 exclusive oversized aggregate 하나로 저장·복원한다. Standalone Actor와 Instance Spot
unit은 gate 안에서만 admit한다. Permit attempt는 all-or-nothing이며 실패한 unit은 일부 permit을 보유한 채
기다리지 않는다.

User Spot aggregate와 maintenance inventory의 권한 원본은 Location Store의 bounded canonical participant set이다.
Relocation manifest는 participant별 state·journal payload를 찾기 위한 projection과 같은 canonical inventory digest를
가질 수 있지만 owner·membership commit의 권한 근거가 아니다. Runtime은 Location participant set의 digest와
Relocation manifest digest가 정확히 같은 경우에만 restore와 replay를 시작한다.

각 tree component의 retention은 24시간이고 renew threshold는 12시간이다. Provider는 Redis server time으로
`ExpiresAt`과 `StoreNow`를 계산한다. Application host wall clock은 retention 판단에 사용하지 않는다.

## 3. Provider operation

Relocation Store는 opaque payload를 저장하고 읽고, retention을 갱신하고, reference를 삭제하는 generic operation만
제공한다. Actor·Spot별 Store interface, relocation phase별 method와 Redis 전용 root 등록 API는 제공하지 않는다.

- Put은 content-addressed reference, checksum, `ExpiresAt`과 `StoreNow`를 반환한다. 같은 content의 uncertain
  completion은 stored bytes를 verify한 뒤 idempotent하게 retry한다.
- Read는 exact reference의 immutable bytes 또는 `Missing` closed result를 반환한다.
- Renew는 complete tree의 retention을 갱신한다. 일부 component만 갱신되면 성공이 아니며 runtime은 root를
  authority에 연결하지 않는다.
- Delete는 exact reference를 idempotent하게 제거한다. 존재하지 않는 reference는 정상 `Missing` 결과다.

Framework가 provider에 넘긴 input bytes는 asynchronous operation이 끝날 때까지 유효하고 변경되지 않는다.
Provider가 그 이후 bytes를 보관하려면 복사한다. Success result bytes는 consumer가 사용하는 동안 immutable하고
stable해야 한다.

## 4. Location authority와 연결 순서

Cross-store 저장 순서는 다음과 같다.

1. Relocation Store에 모든 immutable chunk를 저장한다.
2. Root manifest를 저장하고 reference, checksum, canonical inventory digest와 retention을 검증한다.
3. `Captured` 또는 root replacement authority CAS가 Location Store에 reference, checksum, phase와 count를 연결한다.
4. CAS에 연결되지 않은 root와 chunk는 orphan retention 또는 idempotent cleanup으로 제거한다.

`Captured`와 `Prepared` CAS 직전에 complete tree의 모든 component가 renew threshold보다 긴 remaining lifetime을
갖는지 검증하고 필요하면 tree 전체를 renew한다. Missing 또는 partial renew는 precommit abort이며 Location
authority에 reference를 연결하지 않는다.

Completion append, `replyRelayAck` 또는 exact request-source owner lease expiry를 기록할 때는 새 immutable root를
먼저 만든다. 그 뒤 Location Store expected-version CAS 한 번으로 root reference, checksum,
`TerminalCompletionCount`와 `PendingRelayCount`를 함께 교체한다. Conflict loser의 새 root는 orphan이다.

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

## 7. 검증 요구

- Chunk와 root가 immutable하고 completion append가 새 root를 만든다.
- Relocation manifest의 inventory digest가 Location Store의 authoritative canonical participant set과 일치한다.
- Put과 root 검증이 authority CAS보다 먼저 수행된다.
- CAS conflict로 연결되지 않은 root가 orphan retention 또는 idempotent cleanup으로 제거된다.
- Location authority reference release가 Relocation root delete보다 먼저 수행된다.
- `Recreate`가 application state 없이도 accepted journal과 recovery payload를 Relocation Store에 기록한다.
- Published root의 permanent missing·checksum mismatch·inventory digest mismatch가 `RelocationDataLost`이며 rollback하지
  않는다.
- Location과 Relocation Redis가 같은 deployment와 분리 deployment에서 모두 동작하고 cross-store transaction을
  요구하지 않는다.
