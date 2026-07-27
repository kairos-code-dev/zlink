# Java Location과 relocation 공개 인터페이스

[Java exact interface 목록](README.ko.md) · [공통 Location runtime](../../../../40-location-runtime.ko.md) ·
[공통 Redis provider](../../../../41-location-store-redis.ko.md)

이 문서는 application이 등록하거나 운영 상태를 조회할 때 필요한 Java public contract만 고정한다.
Location provider 구현자는 `ZLinkLocationStore`, relocation payload provider 구현자는
`ZLinkRelocationStore`만 구현한다. Framework 내부 실행 단계를 capability interface로 나누어 공개하지 않는다.

## Provider 등록

`ZLinkFrameworkOptions`의 exact 선언은
[host configuration contract](configuration-host.ko.md)가 단독으로 소유한다. 이 interface의
`addLocationStore(...)`와 `addRelocationStore(...)`로 두 provider를 각각 등록한다.

두 Store를 묶어 등록하는 API와 Redis 전용 등록 API는 없다. 같은 Redis deployment를 사용할 수 있지만
각 Store는 별도 instance와 key prefix를 사용한다.

## Location Store

`ZLinkLocationStore`는 descriptor, owner lease, object authority, placement reservation과 aggregate relocation
commit을 하나의 provider transaction domain에서 처리한다. Provider는 opaque authority payload를 해석하지 않는다.

```java
public interface ZLinkLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateMeshNode(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeMeshNode(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> listMeshNodes(
        String meshName,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationWriteResult> updateClientServer(
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeClientServer(
        ZLinkClientServerServerDescriptorKey key,
        ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkLocationPage<ZLinkClientServerServerDescriptor>> listClientServers(
        String channelName,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationWriteResult> updateFanoutPublisher(
        ZLinkFanoutPublisherDescriptor descriptor,
        ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeFanoutPublisher(
        ZLinkFanoutPublisherDescriptorKey key,
        ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>> listFanoutPublishers(
        String channelName,
        ZLinkPageRequest page);

    CompletionStage<ZLinkOwnerLeaseClaimResult> claimOwnerLease(
        String ownerId,
        Duration leaseTtl);
    CompletionStage<ZLinkOwnerLeaseReadResult> readOwnerLease(String ownerId);
    CompletionStage<ZLinkOwnerLeaseRenewResult> renewOwnerLease(
        ZLinkLocationOwnerToken token,
        Duration leaseTtl);
    CompletionStage<ZLinkOwnerLeaseReleaseResult> releaseOwnerLease(
        ZLinkLocationOwnerToken token);

    CompletionStage<ZLinkAuthorityReadResult> read(
        String key,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkAuthorityWriteResult> compareExchange(
        String key,
        ZLinkAuthorityExpectation expectation,
        ZLinkAuthorityMutation mutation,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkAuthorityScanResult> list(
        String prefix,
        Optional<ZLinkAuthorityScanCursor> cursor,
        int limit,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkObjectReserveResult> reserve(
        ZLinkObjectReservationRequest request,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkObjectRejectResult> reject(
        ZLinkObjectReservation reservation,
        ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        ZLinkCreationOperationTerminal terminal,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkCreationTerminalReadResult> readCreationTerminal(
        ZLinkCreationOperationIdentity operation,
        ZLinkStoreCancellation cancellation);

    CompletionStage<ZLinkRelocationCapacityReserveResult> reserveRelocationCapacity(
        ZLinkRelocationCapacityReservationRequest request,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationCapacityAbortResult> abortRelocationCapacity(
        ZLinkRelocationCapacityFence fence,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
        ZLinkAggregatePrepareRequest request,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation);

    CompletionStage<Long> removeAllByOwner(ZLinkLocationOwnerToken owner);

    default CompletionStage<OptionalLong> getMeshNodeChangeStamp(
        String meshName) {
        return CompletableFuture.completedFuture(OptionalLong.empty());
    }
}
```

`ZLinkStoreCancellation`은 provider I/O 한 operation의 cancellation만 표현한다. Application callback이나
host lifecycle token으로 사용하지 않는다. `byte[]` ownership, generation, CAS, scan cursor와 aggregate
원자성은 공통 spec을 따른다.
`getMeshNodeChangeStamp`는 correctness에 영향을 주지 않는 선택적 polling 최적화다. 기본 구현은
`OptionalLong.empty()`를 반환하며, stamp를 유지하는 provider만 non-negative counter를 반환한다.

Provider package의 public contract에는 Store method가 인자나 반환값으로 직접 사용하고, 그 결과 union을
해석하는 데 필요한 타입만 포함한다. 다음 목록은 이 transitive public boundary의 전체 범위다. Runtime의
publisher, resolver, cache, retry coordinator와 Redis serialization type은 이 목록에 포함하지 않는다.

- descriptor write: `ZLinkMeshNodeDescriptor`, `ZLinkMeshNodeDescriptorKey`,
  `ZLinkClientServerServerDescriptor`, `ZLinkClientServerServerDescriptorKey`,
  `ZLinkFanoutPublisherDescriptor`, `ZLinkFanoutPublisherDescriptorKey`, `ZLinkLocationWriteIntent`,
  `ZLinkLocationWriteResult`, `ZLinkLocationWriteStatus`, `ZLinkLocationOwnerToken`, `ZLinkLocationPage`,
  `ZLinkPageRequest`
- owner lease: `ZLinkOwnerLeaseClaimResult`, `ZLinkOwnerLeaseReadResult`, `ZLinkOwnerLeaseRenewResult`,
  `ZLinkOwnerLeaseReleaseResult`와 각 sealed result의 permitted record
- authority CAS: `ZLinkAuthorityExpectation`, `ZLinkAuthorityMutation`, `ZLinkAuthorityReadResult`,
  `ZLinkAuthorityWriteResult`, `ZLinkAuthorityScanCursor`, `ZLinkAuthorityScanResult`와 각 sealed union의
  permitted record
- object creation: `ZLinkObjectReservationRequest`, `ZLinkObjectReservation`, `ZLinkObjectReserveResult`,
  `ZLinkObjectCommitResult`, `ZLinkObjectRejectResult`, `ZLinkObjectAbortResult`,
  `ZLinkCreationOperationIdentity`, `ZLinkCreationOperationTerminal`, `ZLinkCreationTerminalReadResult`와 각
  sealed result의 permitted record
- relocation commit: `ZLinkRelocationCapacityReservationRequest`, `ZLinkRelocationCapacityFence`,
  `ZLinkRelocationCapacityReserveResult`, `ZLinkRelocationCapacityAbortResult`, `ZLinkAggregatePrepareRequest`,
  `ZLinkAggregateFence`, `ZLinkAggregatePrepareResult`, `ZLinkAggregateCommitResult`,
  `ZLinkAggregateAbortResult`와 각 sealed result의 permitted record
- provider I/O: `ZLinkStoreCancellation`

Authority mutation은 일반 write, startup recovery와 delete를 닫힌 union으로 구분한다. `Restore`는 exact
StoreVersion과 owner token을 함께 확인하지만 owner lease의 live 상태는 요구하지 않는다. 따라서 owner lease가
만료된 뒤에도 root 없는 `Preparing` payload만 steady payload로 안전하게 되돌릴 수 있다.

```java
public sealed interface ZLinkAuthorityMutation
    permits ZLinkAuthorityPut,
            ZLinkAuthorityRestore,
            ZLinkAuthorityDelete {}

public record ZLinkAuthorityRestore(
    byte[] payload,
    ZLinkLocationOwnerToken expectedOwner)
    implements ZLinkAuthorityMutation {}
```

## Relocation Store

```java
public interface ZLinkRelocationStore {
    CompletionStage<ZLinkRelocationStored> put(
        byte[] payload,
        Duration retention,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationReadResult> get(
        String reference,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationRenewResult> renew(
        String reference,
        Duration retention,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationDeleteResult> delete(
        String reference,
        ZLinkStoreCancellation cancellation);
}
```

Relocation payload provider가 추가로 알아야 하는 transitive public type은 `ZLinkRelocationStored`,
`ZLinkRelocationReadResult`, `ZLinkRelocationRenewResult`, `ZLinkRelocationDeleteResult`와 각 sealed result의
permitted record뿐이다. Payload key codec, manifest parser, retention scheduler와 orphan collector는 internal이다.

Relocation Store는 immutable payload를 먼저 저장한다. Location Store의 CAS가 reference를 publish한 뒤에만
runtime이 해당 payload를 복원 근거로 사용한다. 두 Store 사이의 distributed transaction은 요구하지 않는다.

## 운영 조회

```java
public interface ZLinkLocationRuntimeQuery {
    CompletionStage<ZLinkLocationRuntimeStatus> getStatus();
    CompletionStage<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> listMeshNodes(
        String meshName,
        ZLinkPageRequest page);
    CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> listTopology(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page);
    CompletionStage<ZLinkLocationPage<ZLinkLocationServiceSummary>> listServiceSummaries(
        ZLinkLocationServiceSummaryFilter filter,
        ZLinkPageRequest page);
}

public interface ZLinkLocationReadiness {
    CompletionStage<Boolean> isPeerReady(
        String meshName,
        ZLinkLocationRole role,
        RoutingId nodeRid);
}
```

Topology는 MeshNode descriptor와 owner lease liveness만 투영한다. Spot·Actor authority는 resolve 대상이며
운영 topology 목록에 포함하지 않는다.

```java
public record ZLinkLocationTopologyFilter(
    String meshName,
    RoutingId nodeRid,
    ZLinkLocationTopologyState state) {}

public record ZLinkLocationTopologyEntry(
    String meshName,
    RoutingId nodeRid,
    String endpoint,
    boolean draining,
    ZLinkLocationTopologyState state,
    Instant updatedAt) {}
```

`ZLinkLocationReadiness`는 application이 실제 operation 전에 필요한 readiness를 확인하는 공개 query다.
운영 조회는 bounded page만 반환한다. Watch publisher, 별도 change-stamp Store나 event, raw Spot·Actor location row와 key,
`ZLinkLocationAutoConnectType`, routing-ID slot/group는 public contract가 아니다.

## Redis extension

```java
public final class ZLinkRedisLocationStore
    implements ZLinkLocationStore, AutoCloseable {
    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options);
}

public final class ZLinkRedisRelocationStore
    implements ZLinkRelocationStore, AutoCloseable {
    public ZLinkRedisRelocationStore(ZLinkRedisRelocationOptions options);
}

public final class ZLinkRedisLocationOptions {
    public String connectionString();
    public ZLinkRedisLocationOptions setConnectionString(String value);
    public String keyPrefix();
    public ZLinkRedisLocationOptions setKeyPrefix(String value);
    public Duration commandTimeout();
    public ZLinkRedisLocationOptions setCommandTimeout(Duration value);
}

public final class ZLinkRedisRelocationOptions {
    public String connectionString();
    public ZLinkRedisRelocationOptions setConnectionString(String value);
    public String keyPrefix();
    public ZLinkRedisRelocationOptions setKeyPrefix(String value);
    public Duration commandTimeout();
    public ZLinkRedisRelocationOptions setCommandTimeout(Duration value);
}
```

두 options class는 `connectionString`, `keyPrefix`, 선택적인 positive `commandTimeout`만 제공한다.
Redis script client, key codec와 row serializer는 extension 내부 구현이며 public contract가 아니다.

## 공개하지 않는 계약

다음 타입은 provider가 별도로 구현하거나 application이 호출하는 interface가 아니다.

- descriptor·owner lease·authority를 쪼갠 capability Store
- location watch, change stamp와 runtime invalidation hook
- peer·Spot·Actor·route별 raw Store와 resolver
- authority publisher, handler invocation wrapper와 serializer 선택 helper
- routing-ID slot, allocation group와 allocated-RID provider

이 기능을 별도 interface로 공개하는 대안은 작은 구현을 쉽게 만들지만, provider가 Framework state machine의
분해 방식과 호출 순서를 알아야 한다. 하나의 깊은 `ZLinkLocationStore`로 묶는 계약은 구현해야 할 interface와
registration을 줄이고 transaction 경계를 Store 내부에 숨기므로 이 계약을 사용한다.
