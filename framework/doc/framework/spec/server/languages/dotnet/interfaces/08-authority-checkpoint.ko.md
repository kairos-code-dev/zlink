# .NET authority와 checkpoint provider 공개 인터페이스

[.NET exact interface 목차](README.ko.md) · [Location record](08-location-maintenance.ko.md) ·
[공통 Location runtime](../../../40-location-runtime.ko.md)

## 1. 범위

이 문서는 Actor와 Instance Spot owner·transfer authority를 원자적으로 바꾸는 provider capability와 immutable
checkpoint bytes를 보관하는 provider capability를 고정한다. 두 interface는 provider 구현자를 위한 extension
surface다. Application service code는 이 interface를 호출하거나 key, store version, payload, checkpoint
reference와 retention을 조립하지 않는다.

## 2. Authority Store

```csharp
public readonly record struct ZLinkAuthorityKey(string Value);

public sealed record ZLinkAuthoritySnapshot(
    string StoreVersion,
    ReadOnlyMemory<byte> Payload,
    ulong ObjectGeneration,
    ulong AuthorityOwnerGeneration,
    DateTimeOffset StoreNow);

public abstract record ZLinkAuthorityReadResult
{
    private protected ZLinkAuthorityReadResult() { }
    public sealed record Missing(
        DateTimeOffset StoreNow) : ZLinkAuthorityReadResult;
    public sealed record Found(
        ZLinkAuthoritySnapshot Snapshot) : ZLinkAuthorityReadResult;
}

public abstract record ZLinkAuthorityExpectation
{
    private protected ZLinkAuthorityExpectation() { }
    public sealed record Missing : ZLinkAuthorityExpectation;
    public sealed record Found(
        string StoreVersion) : ZLinkAuthorityExpectation;
}

public sealed record ZLinkAuthorityEntry(
    ZLinkAuthorityKey Key,
    ZLinkAuthoritySnapshot Snapshot);

public readonly record struct ZLinkAuthorityScanCursor
{
    public ZLinkAuthorityScanCursor(string encoded);
    public string Encoded { get; }
}

public sealed record ZLinkAuthorityPage(
    IReadOnlyList<ZLinkAuthorityEntry> Items,
    ZLinkAuthorityScanCursor? NextCursor);

public abstract record ZLinkAuthorityScanResult
{
    private protected ZLinkAuthorityScanResult() { }
    public sealed record Page(
        ZLinkAuthorityPage Value) : ZLinkAuthorityScanResult;
    public sealed record ScanExpired : ZLinkAuthorityScanResult;
}

public abstract record ZLinkAuthorityMutation
{
    private protected ZLinkAuthorityMutation() { }
    public sealed record Put(
        ReadOnlyMemory<byte> Payload,
        ZLinkAuthorityGenerationTransition GenerationTransition)
        : ZLinkAuthorityMutation;
    public sealed record Delete : ZLinkAuthorityMutation;
}

public enum ZLinkAuthorityGenerationTransition
{
    Preserve = 1,
    NewOwner = 2,
    NewObject = 3
}

public abstract record ZLinkAuthorityCompareExchangeResult
{
    private protected ZLinkAuthorityCompareExchangeResult() { }
    public sealed record Stored(
        ZLinkAuthoritySnapshot Snapshot) : ZLinkAuthorityCompareExchangeResult;
    public sealed record Deleted(
        string StoreVersion,
        DateTimeOffset StoreNow) : ZLinkAuthorityCompareExchangeResult;
    public sealed record Conflict(
        ZLinkAuthorityReadResult Current) : ZLinkAuthorityCompareExchangeResult;
    public sealed record GenerationExhausted : ZLinkAuthorityCompareExchangeResult;
}

public interface IZLinkAuthorityStore
{
    ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkAuthorityCompareExchangeResult> CompareExchangeAuthorityAsync(
        ZLinkAuthorityKey key,
        ZLinkAuthorityExpectation expectation,
        ZLinkAuthorityMutation mutation,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
        string prefix,
        ZLinkAuthorityScanCursor? cursor,
        int limit,
        CancellationToken cancellationToken = default);
}
```

`StoreVersion`, generation과 `StoreNow`는 provider가 발급한다. Missing은 `StoreNow`만 반환하고 fake
StoreVersion을 갖지 않는다. `CompareExchangeAuthorityAsync`는 `ZLinkAuthorityExpectation`을 받는
overload만 제공한다. `NewObject`는 `Missing`을, `Preserve`·`NewOwner`·delete는 current StoreVersion을
담은 `Found`를 요구한다. `ListAuthoritiesAsync`의 first page는 `cursor=null`로 요청한다. Provider는 한
snapshot을 만들고 이어지는 page에 필요한 모든 상태를 하나의 `ZLinkAuthorityScanCursor`에 담는다. 다음
page는 직전 page의 `NextCursor` 값을 해석하거나 다시 조립하지 않고 그대로 넘긴다. Cursor의 UTF-8 encoded
크기는 `1..4096` bytes이며 empty cursor는 허용하지 않는다. Constructor는 범위를 검증하고 immutable
`string` 값을 보관한다. Provider는 snapshot에 포함된 key incarnation을 scan 전체에서 각각 한 번만
반환한다. Concurrent delete는 Framework의 exact read에서 Missing으로 제거되고 snapshot 뒤의
create·recreate는 다음 scan에서 반환된다. Framework는 각 candidate를
exact read한 뒤 current StoreVersion으로 CAS한다. 등록한 MeshName scope의 initial scan이 완료되기
전에는 Serving을 게시하지 않고, 이후 scan은 background recovery로 반복한다. Page는 opaque
key와 payload를 반환한다. Framework가 operational Actor projection을 decode하며 provider는 key와 payload를
해석하지 않는다.
Provider가 cursor가 가리키는 scan을 만료시켰으면 이어지는 page 요청은 `ScanExpired`를 반환한다.
Framework는 부분 결과를 사용하지 않고 first page부터 새 scan을 시작한다.
Provider domain은 영구적인 global object generation, authority owner generation과 Store revision counter를
각각 하나씩 유지한다. CAS 성공 operation에서 `NewObject`는 object와 owner generation을 모두
증가시키고, `NewOwner`는 owner generation만 증가시키며 `Preserve`는 둘 다 유지한다. Stored
mutation과 delete는 global Store revision으로 fence한다. Delete는 row를 완전히 제거하고 per-key counter나
version tombstone을 유지하지 않는다. Scan lease가 활성화된 동안만 scan snapshot을 유지하기 위한
tombstone을 bounded로 유지할 수 있다. Payload에 generation을 중복 encode하지 않는다. Authority row는
TTL을 갖지 않고 explicit fenced delete가 성공할 때까지
유지된다. Owner·coordinator lease는 별도 token row에 저장하며 lease 만료나 reclaim이 authority row를
삭제하거나 수정하지 않는다. Transfer phase와 recovery cursor는 opaque payload에 둔다.

`IZLinkAuthorityStore`는 별도로 등록하지 않는다. Root에 등록하는 `IZLinkLocationStore`가 이 capability를
상속하며 같은 provider instance가 location owner와 transfer authority를 한 transaction domain에서 처리한다.
Provider는 Instance Spot이나 Actor transfer phase별 interface를 추가로 구현하지 않는다.

한 authority opaque payload의 encoded 크기는 최대 1 MiB다. Scan `limit`은 `1..1000`이고 provider는
encoded page 4 MiB에 먼저 도달하면 요청보다 적은 entry와 `NextCursor`를 반환한다. 이 byte limit을
바꾸는 public option은 없다. Hot authority row는 compact metadata와 replay cursor만 보관하며 complete terminal
reply bytes는 checkpoint stream에 저장한다.

세 counter는 `1..long.MaxValue` 범위이며 wrap하거나 재사용하지 않는다. CAS 성공에 새 StoreVersion,
ObjectGeneration 또는 AuthorityOwnerGeneration이 필요한데 해당 global counter가 최댓값이면 provider는
non-retriable `GenerationExhausted`를 반환한다. 이 결과는 row, index와 모든 counter를 바꾸거나 값을 소비하지
않는다. 외부 상태가 바뀌지 않은 채 같은 expectation을 다시 제출하면 같은 결과를 반환한다. Transport 또는
provider exception은 이 닫힌 결과와 구분한다. Framework는 기존 lifecycle failure로 operation을 닫으며
application용 error enum을 추가하지 않는다.

## 3. Checkpoint Store

```csharp
public sealed record ZLinkCheckpointStored(
    string Reference,
    DateTimeOffset ExpiresAt,
    DateTimeOffset StoreNow);

public abstract record ZLinkCheckpointReadResult
{
    private protected ZLinkCheckpointReadResult() { }
    public sealed record Found(
        ReadOnlyMemory<byte> Payload) : ZLinkCheckpointReadResult;
    public sealed record Missing : ZLinkCheckpointReadResult;
}

public enum ZLinkCheckpointDeleteResult
{
    Deleted = 0,
    Missing = 1
}

public abstract record ZLinkCheckpointRenewResult
{
    private protected ZLinkCheckpointRenewResult() { }
    public sealed record Renewed(
        DateTimeOffset ExpiresAt,
        DateTimeOffset StoreNow) : ZLinkCheckpointRenewResult;
    public sealed record Missing : ZLinkCheckpointRenewResult;
}

public interface IZLinkCheckpointStore
{
    ValueTask<ZLinkCheckpointStored> PutCheckpointAsync(
        ReadOnlyMemory<byte> payload,
        TimeSpan retention,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkCheckpointReadResult> GetCheckpointAsync(
        string reference,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkCheckpointRenewResult> RenewCheckpointAsync(
        string reference,
        TimeSpan retention,
        CancellationToken cancellationToken = default);
    ValueTask<ZLinkCheckpointDeleteResult> DeleteCheckpointAsync(
        string reference,
        CancellationToken cancellationToken = default);
}
```

Framework는 put과 renew의 `retention`에 정확히 `TimeSpan.FromHours(24)`를 넘긴다. 이 값은 application
option이 아니다. Authority의 current checkpoint reference를 확인한 owner 또는 recovery coordinator만
`RenewCheckpointAsync`를 호출하며, 존재하지 않는 reference는 `Missing` 정상 결과다.
Provider는 자신의 store clock에서 `ExpiresAt`을 계산하고 `Renewed`에 새 expiry와 `StoreNow`를 반환한다.
Runtime은 이 두 값을 다음 renewal 판단에 사용하며 local clock으로 provider expiry를 추측하지 않는다.
Provider는 reference와 payload를 opaque value로 취급한다.
`GetCheckpointAsync`의 `Missing`은 닫힌 결과이고 `DeleteCheckpointAsync`의 `Missing`은 idempotent cleanup
성공이다. Runtime은 completed·aborted transaction의 checkpoint를 즉시 삭제하며 실패나 orphan은 24시간 TTL이
정리한다.

Framework는 logical checkpoint를 immutable 64 MiB chunk 최대 4096개와 root manifest로 내부에서 나누므로
logical state ceiling은 256 GiB다. `IZLinkCheckpointStore`의 opaque put/get interface는 바꾸지 않으며 chunk
크기, 개수와 manifest를 설정하는 public option도 제공하지 않는다. Capture가 ceiling을 넘으면 seal을 되돌려
normal messaging을 다시 허용하고 Retire 결과를 `Blocked`로 종료한다. 일반 message의 negotiated effective
bound는 checkpoint chunk 크기 때문에 줄이지 않는다.

`Recreate`와 `Snapshot` transfer를 등록한 host는 `IZLinkCheckpointStore`를 정확히 하나 등록해야 한다.
`Snapshot` adapter는 typed application state만 받고 `ReadOnlyMemory<byte>`, reference와 retention을 받지 않는다.

Framework가 provider에 넘긴 `ReadOnlyMemory<byte>`의 underlying storage는 asynchronous operation이 끝날 때까지
유효하며 바뀌지 않는다. Provider가 완료 뒤에도 buffer를 보관하려면 먼저 복사해야 한다. Provider가 성공
result로 반환한 payload memory는 result가 사용되는 동안 안정적이어야 하며 provider는 반환 뒤 underlying
storage를 수정하거나 다른 result에 재사용하지 않는다. Mutable buffer 기반 adapter는 provider boundary에서
snapshot을 만든다.

Cancellation이 provider 호출 전에 이미 요청되었으면 Framework는 provider operation을 시작하지 않으므로 I/O와
commit이 없다. Provider operation을 시작한 뒤 waiter가 취소되거나 오류로 끝나면 commit 여부는 알 수 없다.
Authority CAS는 같은 exact key와 expectation의 StoreVersion을 다시 읽어 결과를 reconcile한 뒤 retry한다.
Checkpoint put은 content-addressed reference를 확인한 뒤 idempotent하게 retry한다. Authority에 연결되지 않은
committed put은 orphan이며 고정 retention과 cleanup으로 제거한다. 이 의미를 표현하는 public result는 추가하지
않는다.
