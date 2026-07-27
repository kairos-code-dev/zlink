# Relocation Store provider SPI와 공식 Redis 구현

[공통 스펙 목차](README.ko.md) · [Location runtime](40-location-runtime.ko.md) ·
[Location Store provider SPI](41-location-store-redis.ko.md) ·
[Host relocation와 shutdown](54-graceful-drain-handoff.ko.md)

## 1. 범위와 독자

이 문서는 Relocation Store provider를 구현하는 개발자가 지켜야 하는 공개 SPI를 정의한다. Provider는
Framework가 발급한 reference에 immutable bytes를 저장하고, 같은 reference를 안전하게 다시 읽고 갱신하고
삭제할 수 있게 한다.

Provider는 application state, accepted journal, timer, participant manifest, activation envelope와 relocation
phase의 의미를 해석하지 않는다. Framework가 payload를 구성하고 Location Store authority에 공개하는 순서는
[Location runtime의 Store 사용 순서](40-location-runtime.ko.md#8-store-응답을-받지-못했을-때)가 소유한다.
Instance Spot cold activation과 Actor Join도 [Location runtime](40-location-runtime.ko.md)이 소유한다.

Application은 이 SPI의 operation을 직접 호출하지 않는다. Provider package만 SPI를 구현하며 Framework가
등록된 instance를 사용한다.

## 2. 공개 SPI의 책임

Relocation Store SPI는 다음 operation만 제공한다.

| Operation | Provider가 보장하는 결과 |
|---|---|
| `Put` | Caller가 발급한 reference에 immutable payload를 저장하거나 기존 payload와 exact equality를 확인한다. |
| `Read` | Exact reference의 immutable payload, expiry와 `StoreNow`를 반환한다. |
| `Renew` | Provider clock을 기준으로 retention을 연장한다. |
| `Delete` | Exact reference를 idempotent하게 제거한다. |

SPI type과 interface는 기본 Framework API와 분리된 provider abstraction package 또는 module이 소유한다.
기본 Framework package는 abstraction에 의존하지만 Store operation을 application API로 다시 노출하지 않는다.
외부 provider는 application·Actor·Spot package에 의존하지 않고 abstraction만 구현할 수 있어야 한다.

Relocation phase, manifest, participant, replay cursor와 completion별 public method나 DTO를 추가하지 않는다.
Redis key layout, chunk 자료구조, script와 cleanup index도 공개 SPI에 노출하지 않는다.

다음 .NET 발췌는 공통 SPI의 최소 모양을 보여준다. 정식 선언은
[.NET exact interface](server/languages/dotnet/interfaces/08-authority-relocation.ko.md)에 있다.

```csharp
public interface IZLinkRelocationStore
{
    ValueTask<ZLinkBlobPutResult> PutAsync(
        ZLinkBlobReference reference,
        ReadOnlyMemory<byte> payload,
        TimeSpan retention,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkBlobReadResult> ReadAsync(
        ZLinkBlobReference reference,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkBlobRenewResult> RenewAsync(
        ZLinkBlobReference reference,
        TimeSpan retention,
        CancellationToken cancellationToken = default);

    ValueTask DeleteAsync(
        ZLinkBlobReference reference,
        CancellationToken cancellationToken = default);
}
```

다른 언어의 정식 모양은
[Java](server/languages/java/interfaces/location-maintenance.ko.md),
[Kotlin](server/languages/kotlin/interfaces/location-maintenance.ko.md),
[Node.js](server/languages/node/interfaces/08-location-maintenance.ko.md)와
[C++](server/languages/cpp/interfaces/07-location-store.ko.md) exact interface를 따른다.
두 Store의 등록 예제는 [Location runtime](40-location-runtime.ko.md#13-등록-조건과-수명)에만 둔다.

## 3. Reference, payload와 tree bound

| 항목 | 계약 |
|---|---|
| Reference | Framework가 발급하는 opaque UTF-8 `1..4096` bytes다. Case-sensitive exact match를 사용한다. |
| Blob | 최대 64 MiB인 immutable bytes다. |
| Logical stream | 최대 256 GiB다. |
| Chunk 수 | Root manifest 하나에 최대 4,096개다. |

Provider는 reference를 발급하지 않는다. 같은 content를 저장해도 Framework가 다른 reference를 지정하면
서로 다른 value다. Reference를 삭제하거나 expiry가 지난 뒤 같은 reference에 다른 bytes를 재사용하지 않는다.

Framework가 큰 logical stream을 저장할 때는 최대 64 MiB chunk와 immutable root manifest로 나눈다. Manifest의
logical version, 전체 길이, checksum, chunk 순서와 각 chunk의 reference·길이·checksum은 Framework가 encode한
opaque bytes다. Provider는 tree 구조나 checksum을 해석하지 않는다.

Application state adapter 하나가 반환하는 bytes도 최대 64 MiB다. Process별 relocation payload in-flight 기본
상한 256 MiB는 Framework coordinator의 memory gate이며 Store blob이나 logical stream의 상한이 아니다.

각 tree component의 기본 retention은 24시간이고 renew threshold는 12시간이다. Expiry는 provider clock으로
계산하며 application host의 wall clock은 correctness에 사용하지 않는다.

## 4. Provider operation

### 4.1 Put

`Put(reference, payload, retention)`은 다음 closed result 중 하나를 반환한다.

- `Stored(expiresAt, storeNow)`: Reference가 없어서 payload를 저장했다.
- `AlreadyStored(expiresAt, storeNow)`: 같은 reference에 같은 bytes가 이미 저장되어 있다.
- `Conflict(storeNow)`: 같은 reference에 다른 bytes가 저장되어 있다.

Provider는 content equality를 byte-for-byte로 비교한다. 같은 content에 새 reference를 발급하거나 provider가
선택한 reference를 반환하는 API를 제공하지 않는다.

### 4.2 Read

`Read(reference)`는 exact reference의 bytes·expiry·`StoreNow` 또는 `Missing(StoreNow)`을 반환한다. 만료된
payload는 `Missing`이다. Result bytes는 consumer가 사용하는 동안 변경되거나 다른 result buffer에 재사용되지
않는다.

### 4.3 Renew

`Renew(reference, retention)`은 provider clock을 기준으로 expiry를 연장하고 새 expiry와 `StoreNow`를 반환한다.
Reference가 없거나 만료됐으면 `Missing`이다. 같은 요청을 반복해도 payload bytes는 바뀌지 않는다.

### 4.4 Delete

`Delete(reference)`는 reference가 없어도 성공하는 idempotent operation이다.

## 5. Cancellation, 결과 유실과 cleanup

Operation 시작 전 cancellation은 I/O와 write 시작을 막는다. 시작 뒤 cancellation, timeout 또는 transport
error가 발생하면 저장 여부가 불명확할 수 있다. Framework가 caller-issued reference로 exact read하거나 같은
reference와 bytes로 `Put`을 다시 실행해 결과를 재구성할 수 있어야 한다.

Input bound 위반은 언어별 argument validation error다. `Missing`, `AlreadyStored`와 `Conflict`는 정상적인
closed result다. Provider-specific failure는 Framework가 Store failure로 분류할 수 있어야 하지만 Redis command,
key layout이나 script를 application public API에 노출하지 않는다.

Input bytes는 asynchronous operation이 끝날 때까지 변경되지 않아야 한다. Provider가 그 뒤에도 보관하려면
복사한다.

Location Store authority에 아직 연결되지 않은 payload는 orphan이다. Provider 또는 Framework cleanup은 retention이
끝난 orphan을 제거해야 한다. Published reference는 Location Store에서 사용 종료를 먼저 commit한 뒤에만
삭제한다. 이 순서와 `RelocationDataLost` 처리는
[Location runtime](40-location-runtime.ko.md#8-store-응답을-받지-못했을-때)이 정의한다.

## 6. 등록, 수명과 공식 Redis provider

Provider instance의 등록 조건과 Framework root의 소유권은
[Location runtime의 Store 등록](40-location-runtime.ko.md#1-범위와-책임)을 따른다. Framework가 instance
수명을 소유하는 구성에서는 Store를 사용하는 runtime과 background operation이 모두 끝난 뒤 정확히 한 번
dispose한다. 여러 Store가 물리 connection을 공유할 때 중복 dispose를 막는 책임은 provider 구현에 있다.

공식 Redis extension package는 언어별 naming convention에 맞는 `RedisRelocationStore` 구현을 제공한다.
공개 options는 instance 생성에 필요한 connection, key namespace와 operation timeout으로 제한한다.

Redis key layout, chunk storage 자료구조, script, serialization record, connection lease와 cleanup index는
implementation detail이다. Redis 전용 Framework 등록 helper나 Location·Relocation Store를 함께 구현하는
결합 class를 제공하지 않는다.

Location Store와 Relocation Store는 같은 Redis deployment에서 서로 다른 key namespace를 사용할 수도 있고
물리적으로 분리할 수도 있다. Correctness는 connection 공유나 cross-store Redis transaction에 의존하지 않는다.

## 7. Contract test

- 같은 reference와 같은 bytes를 다시 `Put`하면 `AlreadyStored`, 다른 bytes이면 `Conflict`다.
- 64 MiB blob과 4,096개 chunk로 구성한 256 GiB logical stream 계약을 지원한다.
- `Put` 결과 유실 뒤 caller-issued reference의 exact read나 idempotent retry로 저장 여부를 재구성할 수 있다.
- Read result bytes는 consumer가 사용하는 동안 변경되지 않는다.
- `Renew`와 `Delete` retry가 idempotent하고 expiry는 provider clock을 기준으로 계산된다.
- Published reference를 release하기 전에 payload를 삭제하지 않는다.
- Location Store publish 전에 실패한 payload는 retention 뒤 orphan cleanup 대상이 된다.
- 같은 Redis와 분리 Redis 구성에서 Location Store와 Relocation Store를 각각 등록해 사용할 수 있다.
- Redis provider public declaration에 relocation phase·manifest DTO, script와 key layout type이 없다.
