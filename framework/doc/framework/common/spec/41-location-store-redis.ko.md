# Location Store provider와 공식 Redis 구현

[공통 스펙 목차](README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Relocation Store](42-relocation-store-redis.ko.md) ·
[Host retirement와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위와 공개 경계

Location Store는 Framework가 만든 작은 opaque record를 읽고, version 조건을 만족할 때 bounded key 집합을
하나의 transaction으로 변경하는 provider 확장 지점이다. Provider는 다음 primitive만 구현한다.

- Exact key read
- Missing 또는 expected version 조건을 사용하는 atomic batch write
- Provider clock 기준 TTL
- Recovery와 maintenance를 위한 bounded snapshot scan

Authority, owner lease, descriptor, placement reservation, capacity, membership과 aggregate commit은
Framework 내부 record와 state machine이다. Provider public API에는 이러한 도메인 이름의 method·result·DTO를
두지 않는다. Framework는 record를 opaque bytes로 encode하고 필요한 condition과 mutation을 한 batch에 넣는다.

Application이 사용하는 readiness·runtime query와 host maintenance API는 provider SPI가 아니다. Store instance
등록 뒤 operation은 Framework만 호출한다.

Session socket, binding token과 Actor별 binding route도 Location Store record에 포함하지 않는다. Actor relocation
완료 뒤 route를 바꾸는 작업은 Session owner Framework runtime이 수행한다.

## 2. Store 등록과 수명

Store primitive type과 interface는 기본 Framework API에서 분리한 provider abstraction package 또는 module이
소유한다. 기본 Framework package는 이 abstraction에 의존하지만 application API에 Store operation을
재노출하지 않는다. 외부 provider는 application·Actor·Spot package에 의존하지 않고 abstraction만 구현할 수
있어야 한다.

Location Store와 Relocation Store는 Framework root에 각각 한 번 등록한다. 두 capability를 하나의 Store
interface나 Redis 전용 helper로 묶지 않는다. 같은 역할을 두 번 등록하면 socket bind 전에 startup
configuration error다.

등록이 성공하면 Store instance의 수명은 Framework가 소유한다. Framework는 Store에 의존하는 runtime과
background operation을 먼저 종료한 뒤 Store를 정확히 한 번 dispose한다. 두 Store가 물리 connection을 공유할
때 중복 dispose를 막는 reference ownership은 provider 구현이 관리한다.

Object role이 `Client` 또는 `Server`인 MeshNode는 Location Store를 요구한다. Store가 없으면 startup을 실패하며
runtime-local authority나 hidden fallback Store를 만들지 않는다. Role이 `None`인 MeshNode는 object create,
find, message와 factory를 제공하지 않는다.

## 3. Key, value와 version

| 항목 | 계약 |
|---|---|
| Key | Framework가 발급하는 opaque UTF-8 `1..1024` bytes. Case-sensitive exact match이며 normalization과 case folding을 적용하지 않는다. |
| Value | 최대 1 MiB의 immutable bytes. Expiry가 없으면 durable value다. |
| Version | Provider가 발급하는 opaque UTF-8 `1..4096` bytes. Framework와 provider는 수치 크기나 내부 구조를 해석하지 않는다. |
| Provider clock | Read·commit·scan page와 같은 관측에서 얻은 `StoreNow`. TTL과 expiry correctness의 유일한 wall-clock 기준이다. |

Read는 `Missing(StoreNow)` 또는 `Found(bytes, version, optional expiry, StoreNow)`의 닫힌 결과다. 만료된
value는 `Missing`이다. Provider는 consumer가 결과를 사용하는 동안 bytes를 변경하거나 다른 결과 buffer로
재사용하지 않는다.

Framework의 domain generation은 opaque provider version과 다르다. `ObjectGeneration`,
`AuthorityOwnerGeneration`, owner lease generation과 내부 revision counter는 Framework가 private record에
저장하고 같은 atomic batch에서 expected version을 검사해 증가시킨다. Provider는 이 값의 의미를 해석하거나
별도 domain counter API를 제공하지 않는다.

## 4. Conditional atomic batch

Write request는 condition 집합과 mutation 집합으로 구성한다.

- `Missing(key)`는 key가 없거나 만료된 경우에만 참이다.
- `Version(key, expected)`는 current version이 exact match인 경우에만 참이다.
- `Put(key, bytes, optional retention)`은 새 provider version을 발급한다.
- `Delete(key)`는 key를 제거한다.

Provider는 모든 condition을 먼저 검사하고 모두 참일 때만 모든 mutation을 하나의 commit으로 적용한다.
다른 caller는 commit의 중간 상태를 관찰할 수 없다. Condition 하나라도 거짓이면 닫힌 `Conflict`이며
mutation과 version 증가는 0이다. `Conflict`는 실패한 condition이나 current value를 포함하지 않는다.
Framework는 필요한 key를 exact read하여 retry 또는 terminal 처리를 결정한다.

Batch에는 다음 bound를 적용한다.

- Condition과 mutation의 unique key 합계 최대 2,048개
- Encoded request 최대 4 MiB
- 동일 key의 condition 중복과 mutation 중복 금지
- `Applied` 결과는 각 Put의 새 opaque version과 같은 commit에서 얻은 `StoreNow`를 반환

이 범위는 최대 1,024 aggregate participant와 aggregate·capacity·lease·descriptor 보조 record를 같은
transaction에 포함할 수 있어야 한다. Provider는 participant나 record 종류를 구분하지 않는다.

## 5. Snapshot scan

Scan은 recovery와 maintenance가 Framework record를 bounded하게 찾는 필수 operation이다.

- Prefix는 UTF-8 `0..1024` bytes이며 key와 같은 exact comparison을 사용한다.
- 첫 page는 provider가 bounded snapshot을 만들고 opaque cursor를 반환한다.
- 같은 cursor의 다음 page는 해당 snapshot만 읽는다.
- Page limit은 `1..1000`, page encoded 크기는 최대 4 MiB다.
- Cursor는 opaque UTF-8 `1..4096` bytes다.
- Snapshot을 더 유지할 수 없거나 cursor가 유효하지 않으면 닫힌 `Expired`를 반환한다.

Framework는 `Expired`가 발생하면 받은 부분 결과를 버리고 첫 page부터 다시 읽는다. Scan item은 recovery
후보일 뿐이며 mutation 전에는 exact read와 expected version condition을 다시 사용한다.

Provider는 cursor를 Redis `SCAN` cursor나 내부 collection offset과 같은 형식으로 공개할 필요가 없다.
Cursor의 encoding과 snapshot 보존 구조는 implementation detail이다.

## 6. Framework가 구성하는 상태

Framework는 위 primitive를 사용해 다음 상태를 private record로 구성한다.

- Discovery descriptor와 host owner lease
- Entry Spot ID claim
- Actor·Spot authority와 current owner
- Creation과 relocation capacity reservation
- Actor·Spot membership과 bounded canonical participant set
- Aggregate prepare·commit·abort와 inventory digest
- Creation terminal, relocation phase와 recovery pointer
- Capacity counter와 Framework-owned monotonic generation

같이 변경해야 하는 authority·membership·capacity·aggregate record는 하나의 Location Store batch에 넣는다.
Provider가 domain transition별 public method를 구현하거나 record schema를 알아야 한다는 뜻이 아니다.

Framework-owned generation counter가 `2^63-1`에서 다음 값을 요구하면 Framework는 transaction을 쓰지 않고
non-retriable `GenerationExhausted`로 처리한다. Counter를 wrap하거나 재사용하지 않는다.

Owner lease와 expiring descriptor는 provider clock을 사용한다. Durable authority와 commit record는 명시적인
fenced delete 전까지 유지하며 TTL을 사용하지 않는다. Authority recovery는 scan 결과만 믿지 않고 exact key와
version을 다시 확인한다.

## 7. 결과 유실, 취소와 오류

Provider operation 시작 전 cancellation은 I/O와 commit을 시작하지 않게 한다. Operation이 시작된 뒤
cancellation, timeout 또는 transport 오류가 발생하면 commit 여부가 불확실할 수 있다. Framework는 exact key와
expected version을 다시 읽어 결과를 재조정한다.

입력 bound 위반과 동일 key 중복은 언어별 argument validation error다. `Missing`, `Conflict`와 `Expired`는
정상적인 닫힌 결과이며 provider 장애와 구분한다. Provider-specific failure의 세부 정보는 Framework 내부
diagnostic에 보존할 수 있지만 application public API에는 Redis command, key layout이나 script를 노출하지 않는다.

## 8. 공식 Redis provider

공식 Redis extension package는 Location Store interface를 구현하는 `RedisLocationStore`와 언어별 naming
convention에 맞춘 options를 제공한다. Public surface는 instance 생성에 필요한 connection, key namespace와
operation timeout 설정으로 제한한다.

다음 항목은 Redis provider implementation detail이며 public contract가 아니다.

- Redis key와 hash tag layout
- HASH·SET·ZSET 선택
- Lua script와 transaction 분할 방식
- Private record encoding과 schema marker
- Connection lease, retry와 snapshot cursor 구현
- Change stamp와 polling 최적화

Redis provider는 generic batch의 원자성과 scan snapshot 계약을 충족해야 한다. Domain별 Redis method,
descriptor·authority DTO, change-stamp capability interface와 Redis 전용 Framework 등록 helper는 제공하지 않는다.

Location Store와 Relocation Store는 같은 Redis deployment를 서로 다른 key namespace로 사용할 수 있고,
서로 다른 deployment를 사용할 수도 있다. Public correctness는 connection 공유나 같은 Redis transaction에
의존하지 않는다.

## 9. Contract test

- Read가 bytes·version·optional expiry와 `StoreNow`를 같은 관측으로 반환한다.
- Condition 하나가 실패하면 모든 mutation과 version 증가가 0이다.
- 최대 2,048 unique key와 encoded 4 MiB request가 하나의 atomic commit으로 적용된다.
- 만료된 value는 provider clock 기준 `Missing`이다.
- Durable value는 explicit delete 전까지 TTL 때문에 사라지지 않는다.
- Scan page가 같은 snapshot을 사용하고 snapshot을 유지할 수 없으면 `Expired`를 반환한다.
- Cancellation이나 결과 유실 뒤 exact read와 version으로 commit 여부를 재조정할 수 있다.
- Redis provider public declaration에 authority·reservation·aggregate DTO, script와 key layout type이 없다.
- 같은 Redis와 분리 Redis 구성 모두에서 Location·Relocation Store 등록과 recovery가 동작한다.
