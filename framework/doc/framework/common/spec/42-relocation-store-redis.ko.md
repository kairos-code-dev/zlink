# Relocation Store provider와 공식 Redis 구현

[공통 스펙 목차](README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Location Store](41-location-store-redis.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위와 책임

Relocation Store는 cross-node Actor·Spot 이동과 Instance Spot cold activation에 필요한 immutable payload를
저장하는 provider 확장 지점이다. Provider는 relocation phase, application state, message, timer, journal,
participant와 manifest의 의미를 해석하지 않는다.

Location Store는 owner, membership, relocation phase와 payload reference를 원자적으로 publish한다.
Relocation Store는 publish 전에 준비하는 bytes만 저장한다. 두 Store는 별도 interface와 별도 등록을 사용하며
distributed transaction이나 2PC를 요구하지 않는다.

Relocation Store에는 Framework가 encode한 다음 정보가 포함될 수 있다.

- Application state와 accepted journal
- Seal 시점에 아직 실행하지 않은 message queue
- Timer logical registration과 pending tick
- Participant payload와 replay manifest
- Cross-node accepted completion의 operation ID, optional reply와 retry cursor
- Complete Instance Spot activation envelope와 durable activation inbox의 첫 record

Session binding route는 Relocation Store에 저장하지 않는다. Actor relocation이 완료된 뒤 같은
`ObjectGeneration`을 확인하고 Session owner runtime이 해당 Actor route만 target owner로 갱신한다.

## 2. Reference와 크기

Framework는 put 전에 opaque reference를 발급한다.

| 항목 | 계약 |
|---|---|
| Reference | UTF-8 `1..4096` bytes, case-sensitive exact match |
| Blob | 최대 64 MiB의 immutable bytes |
| Logical stream | 최대 256 GiB |
| Chunk 수 | Root manifest 하나당 최대 4,096개 |

한 번 content에 사용한 reference는 삭제되거나 만료된 뒤에도 다른 bytes에 재사용하지 않는다. Framework는
logical stream을 최대 64 MiB chunk와 immutable root manifest로 나눈다. Manifest에는 logical version,
전체 길이, checksum, chunk 순서와 각 chunk의 reference·길이·checksum을 기록한다.

Application state adapter 하나가 반환하는 bytes도 최대 64 MiB다. Process의 relocation payload in-flight
기본 상한 256 MiB는 Framework coordinator의 memory gate이며 Store blob 크기나 logical stream ceiling이
아니다.

User Spot aggregate와 maintenance inventory의 authoritative participant set은 Location Store에 있다.
Relocation manifest의 participant 목록은 payload 탐색용 projection이며, Location participant set과 manifest의
digest가 일치할 때만 restore와 replay를 시작한다.

## 3. Provider operation

Relocation Store는 다음 generic operation만 제공한다.

### 3.1 Put

`Put(reference, payload, retention)`은 다음 닫힌 결과 중 하나를 반환한다.

- `Stored(expiresAt, storeNow)`: Reference가 없어서 bytes를 저장했다.
- `AlreadyStored(expiresAt, storeNow)`: 같은 reference와 같은 bytes가 이미 저장되어 있다.
- `Conflict(storeNow)`: 같은 reference가 다른 bytes에 사용되었다.

Reference를 caller인 Framework가 먼저 발급하므로 timeout이나 transport 오류로 결과를 받지 못해도 같은
reference를 read하여 저장 여부를 재조정할 수 있다. Provider가 새 reference를 발급하거나 같은 content에
동등하지만 다른 reference를 반환하는 API는 제공하지 않는다.

### 3.2 Read, renew와 delete

- Read는 exact reference의 원래 bytes·expiry·provider clock 또는 닫힌 `Missing`을 반환한다.
- Renew는 provider clock 기준 expiry를 연장하고 새 expiry를 반환한다. 없거나 만료되면 `Missing`이다.
- Delete는 reference가 없어도 성공한 no-op이며 idempotent하다.

Framework가 provider에 넘긴 input bytes는 asynchronous operation이 끝날 때까지 유효하고 변경되지 않는다.
Provider가 이후에도 bytes를 보관하려면 복사한다. Read result의 bytes는 consumer가 사용하는 동안 immutable하고
stable해야 한다.

각 tree component의 기본 retention은 24시간이고 renew threshold는 12시간이다. Expiry는 provider clock으로
계산하며 application host의 wall clock을 correctness에 사용하지 않는다.

## 4. Location authority와 publication 순서

Cross-store publication은 다음 순서로 고정한다.

1. Relocation Store에 immutable chunk를 저장한다.
2. Root manifest를 저장하고 reference, checksum, inventory digest와 retention을 검증한다.
3. Location Store의 expected-version atomic batch로 authority와 reference·checksum을 함께 publish한다.
4. Location batch에 연결되지 않은 root와 chunk는 orphan retention 또는 idempotent cleanup으로 제거한다.

Location Store batch 성공이 payload의 공식 visibility point다. Target은 current Location authority가 연결한
exact reference만 restore 근거로 사용한다. Relocation Store의 payload만으로 owner, membership, phase나 recovery
target을 결정하지 않는다.

Root를 교체할 때는 새 immutable root를 먼저 저장하고 Location Store에서 old reference를 new reference로
조건부 교체한다. Conflict에서 진 새 root는 orphan이다.

삭제는 반대 순서를 사용한다. Location Store에서 reference 사용 종료를 atomic commit한 뒤 blob을 삭제한다.
Commit 전에 blob을 먼저 삭제하지 않는다.

Location authority가 publish한 reference가 영구 `Missing`이거나 checksum·inventory digest가 일치하지 않으면
non-retriable `RelocationDataLost`다. Commit된 owner와 membership을 source로 rollback하거나 이전 root를 추측해
복원하지 않는다.

## 5. Instance Spot cold activation

Instance Spot cold activation도 같은 publication 규칙을 사용한다.

1. Framework는 operation identity, send/request 구분, source route, reply correlation, deadline, target fence,
   metadata와 application payload를 포함한 complete activation envelope를 immutable blob으로 저장한다.
2. Reference, checksum, encoded size와 retention을 확인한다.
3. Location Store atomic batch가 Creating authority, capacity reservation과 recovery reference를 함께 publish한다.
4. Factory와 initialize가 끝나면 root의 first message를 durable activation inbox의 첫 record로 확정한다.
5. Target queue 선두에 first record를 복원한 뒤에만 Ready barrier를 연다.
6. 최초 handler 완료와 replay cursor 갱신을 확인한 뒤 Location Store에서 recovery reference를 release하고
   blob을 삭제한다.

Instance Spot factory를 하나라도 등록한 Object Server는 Relocation Store를 정확히 하나 등록해야 한다.
Relocation policy가 `Disabled`여도 cold activation에는 Relocation Store를 사용한다.

## 6. Actor Join과 completion

Cross-node Actor Join의 accepted completion operation ID는 relocation ID, placement reservation ID와 aggregate
commit ID와 분리한다. Location Store aggregate commit까지 성공한 cross-node `Accepted`만 immutable manifest에
operation ID, optional reply와 retry cursor를 저장해 at-least-once completion을 복구한다.

Same-node Join, `Rejected`와 commit 전 `Failed`는 relocation payload를 만들지 않는다. Actor relocation 뒤
`ObjectGeneration`은 유지한다.

## 7. 취소·오류와 cleanup

Operation 시작 전 cancellation은 I/O와 write를 시작하지 않게 한다. 시작 뒤 cancellation, timeout 또는
transport 오류가 발생하면 저장 여부가 불확실할 수 있다. Framework는 caller-issued reference로 exact read하여
결과를 재조정한다.

`Missing`, `AlreadyStored`와 `Conflict`는 닫힌 정상 결과다. Input bound 위반은 언어별 argument validation
error다. Provider-specific failure는 Framework가 Store failure로 분류하며 application public API에 Redis
command, key layout이나 script를 노출하지 않는다.

Location Store publication 전에 실패한 payload는 orphan이다. Provider 또는 Framework cleanup은 retention이
끝난 orphan을 제거해야 한다. Published reference는 Location Store release 전에는 orphan cleanup 대상이 아니다.

## 8. 공식 Redis provider

공식 Redis extension package는 Relocation Store interface를 구현하는 `RedisRelocationStore`와 언어별 naming
convention에 맞춘 최소 options를 제공한다. Public options는 connection, key namespace와 operation timeout처럼
instance 생성에 필요한 설정으로 제한한다.

Redis key layout, chunk storage 자료구조, script, serialization record, connection lease와 cleanup index는
implementation detail이다. Redis 전용 Framework 등록 helper와 Location·Relocation Store를 함께 구현하는
결합 class는 제공하지 않는다.

같은 Redis deployment를 사용할 때 Location과 Relocation Store는 서로 다른 key namespace를 사용한다.
물리적으로 분리된 Redis도 지원하며 correctness는 cross-store Redis transaction에 의존하지 않는다.

## 9. Contract test

- 같은 reference와 같은 bytes를 다시 put하면 `AlreadyStored`, 다른 bytes면 `Conflict`다.
- 64 MiB blob과 4,096개 chunk로 구성된 256 GiB logical stream 계약을 지원한다.
- Put 결과 유실 뒤 caller-issued reference의 exact read로 저장 여부를 재조정할 수 있다.
- Read result bytes가 consumer 사용 기간에 변경되지 않는다.
- Renew와 delete retry가 idempotent하다.
- Location publication 전에 실패한 payload가 retention 뒤 제거된다.
- Location reference release가 blob delete보다 먼저 수행된다.
- Published root의 permanent missing·checksum mismatch·inventory digest mismatch는
  `RelocationDataLost`이며 source로 rollback하지 않는다.
- Instance cold activation의 complete envelope와 durable first record가 Ready 전에 복원된다.
- Location과 Relocation Redis를 같은 deployment와 분리 deployment에서 모두 사용할 수 있다.
- Redis provider public declaration에 relocation phase·manifest DTO, script와 key layout type이 없다.
