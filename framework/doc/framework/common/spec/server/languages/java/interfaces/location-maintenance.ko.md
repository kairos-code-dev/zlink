# Java Location과 maintenance 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Location runtime](../../../../40-location-runtime.ko.md)

이 문서는 Java에서 Location과 relocation provider를 구성하고 Framework와 연결하는 공개 타입을
고정한다. Application은 provider 구현과 option을 등록하며, Framework가 아래 SPI를 호출해 authority와
relocation state를 관리한다.

```java
// Public declaration excerpt. Method bodies are omitted; the exact JVM
// inventory later in this document is authoritative.
public final class ZLinkLocationOptions {
    public Duration ownerLeaseRenewInterval();
    public void setOwnerLeaseRenewInterval(Duration value);
    public Duration ownerLeaseTtl();
    public void setOwnerLeaseTtl(Duration value);
    public Duration pollingInterval();
    public void setPollingInterval(Duration value);
    public Duration storeFailureGrace();
    public void setStoreFailureGrace(Duration value);
    public Duration ownerLeaseFencingMargin();
    public void setOwnerLeaseFencingMargin(Duration value);
    public Duration ownerLeaseRenewTimeout();
    public void setOwnerLeaseRenewTimeout(Duration value);
    public Duration routeCacheMaxAge();
    public void setRouteCacheMaxAge(Duration value);
    public Duration relocationForwardingWindow();
    public void setRelocationForwardingWindow(Duration value);
    public int maxActiveOutboundRelocations();
    public void setMaxActiveOutboundRelocations(int value);
    public int maxActiveInboundRelocations();
    public void setMaxActiveInboundRelocations(int value);
    public int maxConcurrentRelocationCaptures();
    public void setMaxConcurrentRelocationCaptures(int value);
    public int maxConcurrentRelocationRestores();
    public void setMaxConcurrentRelocationRestores(int value);
    public long maxRelocationPayloadInFlightBytes();
    public void setMaxRelocationPayloadInFlightBytes(long value);
}

public final class ZLinkRedisLocationOptions {
    public String connectionString();
    public ZLinkRedisLocationOptions setConnectionString(String value);
    public String keyPrefix();
    public ZLinkRedisLocationOptions setKeyPrefix(String value);
    public Duration commandTimeout();
    public ZLinkRedisLocationOptions setCommandTimeout(Duration value);
}

public final class ZLinkRedisLocationStore
    implements ZLinkLocationStore,
               ZLinkClientServerLocationStore,
               ZLinkFanoutLocationStore,
               ZLinkPeerLocationStore,
               ZLinkRouteLocationStore,
               ZLinkLocationChangeStampStore,
               AutoCloseable {
    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options);
    public void close();
}

public final class ZLinkRedisRelocationOptions {
    public String connectionString();
    public ZLinkRedisRelocationOptions setConnectionString(String value);
    public String keyPrefix();
    public ZLinkRedisRelocationOptions setKeyPrefix(String value);
    public Duration commandTimeout();
    public ZLinkRedisRelocationOptions setCommandTimeout(Duration value);
}

public final class ZLinkRedisRelocationStore
    implements ZLinkRelocationStore, AutoCloseable {
    public ZLinkRedisRelocationStore(ZLinkRedisRelocationOptions options);
    public void close();
}
```

`ZLinkLocationOptions` 기본값은 `ownerLeaseRenewInterval=5초`, `ownerLeaseTtl=15초`,
`pollingInterval=1초`, `storeFailureGrace=30초`, `ownerLeaseFencingMargin=5초`,
`ownerLeaseRenewTimeout=3초`다. 첫 값은 Location owner lease 갱신 주기이며 service liveness heartbeat가 아니다.
`routeCacheMaxAge` 기본값은 15초, `relocationForwardingWindow` 기본값은 30초다. 0은 각각 cache 또는
forwarding을 끈다. 둘 다 양수이면 cache age가 forwarding window보다 최소 5초 작아야 한다.
Relocation 제한의 기본값은 active outbound 64, active inbound 64, concurrent Capture 8, concurrent Restore 8,
encoded payload in flight 268,435,456 bytes다. 다섯 값은 모두 양수여야 하며 같은 process의 모든 MeshNode가
공유한다. Framework는 active unit, callback과 byte permit을 모두 얻기 전에는 source queue를 seal하지 않는다.
Byte reservation은 Snapshot participant마다 64 MiB와 Framework-owned section의 deterministic encoded upper
bound를 합하며 `Capture` 뒤 actual encoded size로만 축소한다.
Byte 한도를 넘는 단일 User Spot aggregate는 다른 relocation payload 단계와 겹치지 않는 조건으로 단독 실행한다.
Location Store와 [owner lease](../../../../01-glossary.ko.md#owner-lease) runtime을 사용하는 모든 host는
`ownerLeaseRenewInterval + ownerLeaseRenewTimeout < ownerLeaseTtl - ownerLeaseFencingMargin`을 startup에서
검증한다.

공식 Redis extension은 `ZLinkRedisLocationStore`와 `ZLinkRedisRelocationStore`를 별도 class와 options로 제공한다.
한 class가 두 interface를 함께 구현하지 않는다. 두 Store는 같은 Redis deployment를 서로 다른 key prefix로
사용하거나 서로 다른 Redis를 사용할 수 있다. Connection 공유는 구현 세부 사항이고 correctness 조건이 아니다.
같은 deployment에서 prefix가 겹치면 socket bind 전에 startup configuration error로 실패한다.
Redis creation-terminal key의 RID segment는 transport `RoutingId`의 exact raw bytes 길이와 그 raw bytes의
lowercase hex를 사용한다. Canonical hex text를 UTF-8로 다시 encode하지 않는다. Raw bytes가 `node-a`이면
segment는 `6:6e6f64652d61`이다.

`storeFailureGrace`는 discovery reconcile과 새 outbound connect에만 적용한다. Store failure 동안 마지막 stable
desired set을 grace까지 고정하고 existing admitted transport에는 service liveness를 계속 적용한다. Grace 뒤에는
stable store [snapshot](../../../../01-glossary.ko.md#snapshot)을 다시 얻기 전까지 새 connection을 만들지 않는다. 이 값은 owner·coordinator lease나 local
[authority](../../../../01-glossary.ko.md#authority) deadline을 연장하지 않으며 stateful message, timer, factory와 CAS admission은 마지막 valid monotonic
lease [deadline](../../../../01-glossary.ko.md#deadline)에서 닫힌다. Recovery는 exact [owner](../../../../01-glossary.ko.md#owner) token과 stable page set을 재검증한 뒤 diff와 connect를 수행한다.
Missing route 결과는 negative cache에 저장하지 않는다. 다음 operation은 authority를 다시 조회한다.

Object role이 `Client` 또는 `Server`인 [MeshNode](../../../../01-glossary.ko.md#meshnode)는 Store가 필수다. `None`은 manager, [factory](../../../../01-glossary.ko.md#factory), placement와
hidden local object runtime을 만들지 않는다.

Store version과 reference는 provider가 발급한 opaque 값이다. Framework는 이를 수치로 비교하거나
application에 전달하지 않는다.

```java
public sealed interface ZLinkAuthorityReadResult
    permits ZLinkAuthorityMissing, ZLinkAuthoritySnapshot {}

public record ZLinkAuthorityMissing(
    Instant storeNow) implements ZLinkAuthorityReadResult {}

public enum ZLinkPlacementAllocationState {
    RESERVED(1), ACTIVE(2);
    private final int value;
    ZLinkPlacementAllocationState(int value) { this.value = value; }
    public int value() { return value; }
}
public record ZLinkSpotTypeCapacityDelta(
    ZLinkPlacementObjectKind objectKind,
    String stableType,
    int slots) {}

public record ZLinkPlacementCapacityBundle(
    int actorSlots,
    int spotSlots,
    Optional<ZLinkSpotTypeCapacityDelta> spotType) {}

public record ZLinkPlacementAllocation(
    ZLinkPlacementAllocationState state,
    ZLinkPlacementObjectKind objectKind,
    String stableType,
    ZLinkMeshNodeDescriptorKey descriptor,
    long descriptorLifecycleGeneration,
    ZLinkPlacementCapacityBundle capacityBundle) {}

public record ZLinkPendingObjectCreation(
    String reservationId,
    String requestContentReference,
    byte[] requestSha256,
    int requestEncodedSize) {}

public record ZLinkAuthoritySnapshot(
    String storeVersion,
    byte[] payload,
    long objectGeneration,
    long authorityOwnerGeneration,
    String ownerId,
    long ownerLeaseGeneration,
    ZLinkPlacementAllocation allocation,
    Optional<ZLinkPendingObjectCreation> pendingCreation,
    Instant storeNow) implements ZLinkAuthorityReadResult {}

public record ZLinkAuthorityEntry(
    String key,
    ZLinkAuthoritySnapshot snapshot) {}

public record ZLinkAuthorityScanCursor(String encoded) {}

public sealed interface ZLinkAuthorityScanResult
    permits ZLinkAuthorityPage, ZLinkAuthorityScanExpired {}
public record ZLinkAuthorityPage(
    List<ZLinkAuthorityEntry> items,
    Optional<ZLinkAuthorityScanCursor> nextCursor) implements ZLinkAuthorityScanResult {}
public record ZLinkAuthorityScanExpired() implements ZLinkAuthorityScanResult {}

public sealed interface ZLinkAuthorityExpectation
    permits ZLinkAuthorityExpectFound {}
public record ZLinkAuthorityExpectFound(
    String storeVersion) implements ZLinkAuthorityExpectation {}

public sealed interface ZLinkAuthorityMutation
    permits ZLinkAuthorityPut, ZLinkAuthorityDelete {}
public enum ZLinkAuthorityGenerationTransition {
    PRESERVE, NEW_OWNER
}
public record ZLinkRelocationCapacityFence(String value) {}
public record ZLinkAuthorityPut(
    byte[] payload,
    ZLinkAuthorityGenerationTransition generationTransition,
    Optional<ZLinkLocationOwnerToken> targetOwner,
    Optional<ZLinkRelocationCapacityFence> relocationCapacityFence)
    implements ZLinkAuthorityMutation {}
public record ZLinkAuthorityDelete() implements ZLinkAuthorityMutation {}

public sealed interface ZLinkAuthorityWriteResult
    permits ZLinkAuthorityStored, ZLinkAuthorityDeleted, ZLinkAuthorityConflict,
            ZLinkAuthorityGenerationExhausted {}
public record ZLinkAuthorityStored(
    String storeVersion,
    byte[] payload,
    long objectGeneration,
    long authorityOwnerGeneration,
    String ownerId,
    long ownerLeaseGeneration,
    ZLinkPlacementAllocation allocation,
    Instant storeNow)
    implements ZLinkAuthorityWriteResult {}
public record ZLinkAuthorityDeleted(String storeVersion, Instant storeNow)
    implements ZLinkAuthorityWriteResult {}
public record ZLinkAuthorityConflict(ZLinkAuthorityReadResult current)
    implements ZLinkAuthorityWriteResult {}
public record ZLinkAuthorityGenerationExhausted()
    implements ZLinkAuthorityWriteResult {}

public interface ZLinkAuthorityStore {
    CompletionStage<ZLinkAuthorityReadResult> read(
        String key, ZLinkStoreCancellation cancellation);
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
        ZLinkObjectReservationRequest request, ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation, byte[] readyPayload,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkObjectCreationCompleteResult> completeCreation(
        ZLinkObjectReservation reservation,
        ZLinkObjectCreationCompletion completion,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkCreationTerminalReadResult> readCreationTerminal(
        ZLinkCreationOperationIdentity operation,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation, ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationCapacityReserveResult> reserveRelocationCapacity(
        ZLinkRelocationCapacityReservationRequest request,
        ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationCapacityAbortResult> abortRelocationCapacity(
        ZLinkRelocationCapacityFence fence, ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
        ZLinkAggregatePrepareRequest request, ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
        ZLinkAggregateFence fence, ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
        ZLinkAggregateFence fence, ZLinkStoreCancellation cancellation);
}

public record ZLinkRelocationStored(
    String reference, long checksumCrc32c, Instant expiresAt, Instant storeNow) {}
public sealed interface ZLinkRelocationReadResult
    permits ZLinkRelocationFound, ZLinkRelocationMissing {}
public record ZLinkRelocationFound(byte[] payload)
    implements ZLinkRelocationReadResult {}
public record ZLinkRelocationMissing() implements ZLinkRelocationReadResult {}
public enum ZLinkRelocationDeleteResult { DELETED, MISSING }
public sealed interface ZLinkRelocationRenewResult
    permits ZLinkRelocationRenewed, ZLinkRelocationRenewMissing {}
public record ZLinkRelocationRenewed(Instant expiresAt, Instant storeNow)
    implements ZLinkRelocationRenewResult {}
public record ZLinkRelocationRenewMissing() implements ZLinkRelocationRenewResult {}

public interface ZLinkRelocationStore {
    CompletionStage<ZLinkRelocationStored> put(
        byte[] payload, Duration retention, ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationReadResult> get(
        String reference, ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationRenewResult> renew(
        String reference, Duration retention, ZLinkStoreCancellation cancellation);
    CompletionStage<ZLinkRelocationDeleteResult> delete(
        String reference, ZLinkStoreCancellation cancellation);
}

`checksumCrc32c`는 저장된 immutable root bytes의 CRC32C(Castagnoli)를 나타내는
`0..0xFFFF_FFFFL` 범위의 `long`이다. Provider가 이 범위를 벗어난 값을 반환하면 contract violation이다.
Runtime은 이 값과 Location authority에 publish할 u32 checksum이 정확히 같은지 검증한다.

public interface ZLinkStoreCancellation {
    boolean isCancellationRequested();
}

public enum ZLinkPlacementObjectKind {
    ACTOR(1), USER_SPOT(2), INSTANCE_SPOT(3);
    private final int value;
    ZLinkPlacementObjectKind(int value) { this.value = value; }
    public int value() { return value; }
}
public enum ZLinkMeshNodeObjectRole {
    NONE(0), CLIENT(1), SERVER(2);
    private final int value;
    ZLinkMeshNodeObjectRole(int value) { this.value = value; }
    public int value() { return value; }
}
public enum ZLinkObjectMaintenancePolicyKind {
    DISABLED(1), RECREATE(2), SNAPSHOT(3);
    private final int value;
    ZLinkObjectMaintenancePolicyKind(int value) { this.value = value; }
    public int value() { return value; }
}

public record ZLinkObjectCapability(
    ZLinkPlacementObjectKind objectKind,
    String stableType,
    ZLinkObjectMaintenancePolicyKind policy,
    boolean hasSnapshotAdapter,
    int spotLimit) {}

public record ZLinkCapacityUsage(int active, int reserved, int limit) {}

public record ZLinkSpotTypeCapacity(
    ZLinkPlacementObjectKind objectKind,
    String stableType,
    ZLinkCapacityUsage usage) {}

public record ZLinkPlacementCapacity(
    ZLinkCapacityUsage actors,
    ZLinkCapacityUsage spots,
    List<ZLinkSpotTypeCapacity> spotTypes) {}

public record ZLinkObjectReservationRequest(
    ZLinkPlacementObjectKind objectKind, String authorityKey, String stableType,
    String creationIntentReference, byte[] creationIntentHash,
    int creationIntentEncodedSize, ZLinkMeshNodeDescriptorKey targetDescriptor,
    long targetDescriptorLifecycleGeneration,
    ZLinkLocationOwnerToken targetOwner, byte[] creatingPayload,
    ZLinkPlacementCapacityBundle capacityBundle) {}
public record ZLinkObjectReservation(
    String authorityKey, String storeVersion, long objectGeneration,
    long authorityOwnerGeneration, String reservationVersion,
    ZLinkMeshNodeDescriptorKey targetDescriptor,
    long targetDescriptorLifecycleGeneration,
    ZLinkLocationOwnerToken targetOwner) {}
public sealed interface ZLinkObjectReserveResult
    permits ZLinkObjectReserved, ZLinkObjectConflict, ZLinkObjectAlreadyExists,
            ZLinkObjectTypeMismatch, ZLinkPlacementCapacityExhausted,
            ZLinkObjectGenerationExhausted {}
public record ZLinkObjectReserved(ZLinkObjectReservation reservation)
    implements ZLinkObjectReserveResult {}
public record ZLinkObjectConflict(ZLinkAuthorityReadResult current)
    implements ZLinkObjectReserveResult {}
public record ZLinkObjectAlreadyExists(ZLinkAuthoritySnapshot current)
    implements ZLinkObjectReserveResult {}
public record ZLinkObjectTypeMismatch(ZLinkAuthoritySnapshot current)
    implements ZLinkObjectReserveResult {}
public record ZLinkPlacementCapacityExhausted() implements ZLinkObjectReserveResult {}
public record ZLinkObjectGenerationExhausted() implements ZLinkObjectReserveResult {}
public enum ZLinkObjectCommitResult {
    COMMITTED(1), ALREADY_COMMITTED(2), STALE(3), GENERATION_EXHAUSTED(4);
    private final int value;
    ZLinkObjectCommitResult(int value) { this.value = value; }
    public int value() { return value; }
}
public record ZLinkCreationOperationIdentity(
    RoutingId sourceNodeRid, long sourceNodeGeneration,
    long operationIdHigh, long operationIdLow) {}
public enum ZLinkCreationTerminalState { CREATED, REJECTED, FAILED }
public record ZLinkCreationOperationTerminal(
    ZLinkCreationOperationIdentity operation,
    ZLinkCreationTerminalState state,
    byte[] terminalEnvelope,
    byte[] terminalEnvelopeSha256,
    Instant expiresAt) {}
public sealed interface ZLinkCreationTerminalReadResult
    permits ZLinkCreationTerminalFound, ZLinkCreationTerminalMissing {}
public record ZLinkCreationTerminalFound(
    ZLinkCreationOperationTerminal terminal)
    implements ZLinkCreationTerminalReadResult {}
public record ZLinkCreationTerminalMissing(Instant storeNow)
    implements ZLinkCreationTerminalReadResult {}
public sealed interface ZLinkObjectCreationCompletion
    permits ZLinkObjectCreationCreated, ZLinkObjectCreationRejected,
            ZLinkObjectCreationFailed {}
public sealed interface ZLinkObjectCreationCompleteResult
    permits ZLinkObjectCreationCompleted, ZLinkObjectCreationAlreadyCompleted,
            ZLinkObjectCreationCompletionStale,
            ZLinkObjectCreationCompletionGenerationExhausted {}
public enum ZLinkObjectAbortResult {
    ABORTED(1), ALREADY_ABORTED(2), STALE(3);
    private final int value;
    ZLinkObjectAbortResult(int value) { this.value = value; }
    public int value() { return value; }
}

public record ZLinkRelocationCapacityReservationRequest(
    UUID reservationId,
    String authorityKey,
    String expectedStoreVersion,
    ZLinkPlacementObjectKind objectKind,
    String stableType,
    ZLinkMeshNodeDescriptorKey sourceDescriptor,
    long sourceDescriptorLifecycleGeneration,
    ZLinkLocationOwnerToken sourceOwner,
    ZLinkMeshNodeDescriptorKey targetDescriptor,
    long targetDescriptorLifecycleGeneration,
    ZLinkLocationOwnerToken targetOwner,
    ZLinkPlacementCapacityBundle capacityBundle) {}
public sealed interface ZLinkRelocationCapacityReserveResult
    permits ZLinkRelocationCapacityReserved, ZLinkRelocationCapacityAlreadyReserved,
            ZLinkRelocationCapacityConflict, ZLinkRelocationCapacityTargetUnavailable,
            ZLinkRelocationCapacityExhausted {}
public record ZLinkRelocationCapacityReserved(ZLinkRelocationCapacityFence fence)
    implements ZLinkRelocationCapacityReserveResult {}
public record ZLinkRelocationCapacityAlreadyReserved(ZLinkRelocationCapacityFence fence)
    implements ZLinkRelocationCapacityReserveResult {}
public record ZLinkRelocationCapacityConflict(ZLinkAuthorityReadResult current)
    implements ZLinkRelocationCapacityReserveResult {}
public record ZLinkRelocationCapacityTargetUnavailable()
    implements ZLinkRelocationCapacityReserveResult {}
public record ZLinkRelocationCapacityExhausted()
    implements ZLinkRelocationCapacityReserveResult {}
public enum ZLinkRelocationCapacityAbortResult {
    ABORTED, ALREADY_ABORTED, ALREADY_COMMITTED, STALE
}

public record ZLinkAggregateParticipant(
    String authorityKey, String expectedStoreVersion,
    ZLinkAuthorityGenerationTransition ownerTransition,
    byte[] authorityPayload, byte[] membershipMutation) {}
public record ZLinkAggregatePrepareRequest(
    UUID aggregateId, long aggregateGeneration,
    List<ZLinkAggregateParticipant> participants,
    byte[] inventoryDigest,
    ZLinkMeshNodeDescriptorKey targetDescriptor,
    long targetDescriptorLifecycleGeneration,
    ZLinkPlacementCapacityBundle capacityBundle,
    ZLinkLocationOwnerToken targetOwner) {}
public record ZLinkAggregateFence(UUID aggregateId, long aggregateGeneration) {}
public sealed interface ZLinkAggregatePrepareResult
    permits ZLinkAggregatePrepared, ZLinkAggregateAlreadyPrepared,
            ZLinkAggregateConflict, ZLinkAggregateStale,
            ZLinkAggregateGenerationExhausted {}
public record ZLinkAggregatePrepared(ZLinkAggregateFence fence)
    implements ZLinkAggregatePrepareResult {}
public record ZLinkAggregateAlreadyPrepared(ZLinkAggregateFence fence)
    implements ZLinkAggregatePrepareResult {}
public record ZLinkAggregateConflict() implements ZLinkAggregatePrepareResult {}
public record ZLinkAggregateStale() implements ZLinkAggregatePrepareResult {}
public record ZLinkAggregateGenerationExhausted() implements ZLinkAggregatePrepareResult {}
public enum ZLinkAggregateCommitResult {
    COMMITTED(1), ALREADY_COMMITTED(2), STALE(3), GENERATION_EXHAUSTED(4);
    private final int value;
    ZLinkAggregateCommitResult(int value) { this.value = value; }
    public int value() { return value; }
}
public enum ZLinkAggregateAbortResult {
    ABORTED(1), ALREADY_ABORTED(2), STALE(3);
    private final int value;
    ZLinkAggregateAbortResult(int value) { this.value = value; }
    public int value() { return value; }
}

public record ZLinkLocationOwnerToken(String ownerId, long leaseGeneration) {}

public sealed interface ZLinkOwnerLeaseClaimResult
    permits ZLinkOwnerLeaseClaimed, ZLinkOwnerLeaseClaimConflict,
            ZLinkOwnerLeaseGenerationExhausted {}
public record ZLinkOwnerLeaseClaimed(
    ZLinkLocationOwnerToken token,
    Instant leaseExpiresAt,
    Instant storeNow) implements ZLinkOwnerLeaseClaimResult {}
public record ZLinkOwnerLeaseClaimConflict()
    implements ZLinkOwnerLeaseClaimResult {}
public record ZLinkOwnerLeaseGenerationExhausted()
    implements ZLinkOwnerLeaseClaimResult {}

public sealed interface ZLinkOwnerLeaseRenewResult
    permits ZLinkOwnerLeaseRenewed, ZLinkOwnerLeaseRenewStale {}
public record ZLinkOwnerLeaseRenewed(
    Instant leaseExpiresAt,
    Instant storeNow) implements ZLinkOwnerLeaseRenewResult {}
public record ZLinkOwnerLeaseRenewStale()
    implements ZLinkOwnerLeaseRenewResult {}

public enum ZLinkOwnerLeaseReleaseResult { RELEASED, STALE }

public sealed interface ZLinkOwnerLeaseReadResult
    permits ZLinkOwnerLeaseFound, ZLinkOwnerLeaseMissing {}
public record ZLinkOwnerLeaseFound(
    ZLinkLocationOwnerToken token,
    Instant leaseExpiresAt,
    Instant storeNow) implements ZLinkOwnerLeaseReadResult {}
public record ZLinkOwnerLeaseMissing() implements ZLinkOwnerLeaseReadResult {}

public interface ZLinkOwnerLeaseStore {
    CompletionStage<ZLinkOwnerLeaseClaimResult> claimOwnerLease(
        String ownerId, Duration leaseTtl);
    CompletionStage<ZLinkOwnerLeaseReadResult> readOwnerLease(String ownerId);
    CompletionStage<ZLinkOwnerLeaseRenewResult> renewOwnerLease(
        ZLinkLocationOwnerToken token, Duration leaseTtl);
    CompletionStage<ZLinkOwnerLeaseReleaseResult> releaseOwnerLease(
        ZLinkLocationOwnerToken token);
}

public record ZLinkMeshNodeDescriptor(
    String meshName,
    RoutingId rid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    Optional<String> entrySpotId,
    Map<String, Integer> channelWeights,
    long applicationVersion,
    List<ZLinkObjectCapability> objectCapabilities,
    ZLinkMeshNodeObjectRole objectRole,
    int placementWeight,
    ZLinkPlacementCapacity capacity,
    Optional<String> maintenanceWave,
    ZLinkFrameworkRuntimeState state,
    String securityIdentity,
    String ownerId,
    long leaseGeneration,
    Instant updatedAt) {}
public record ZLinkMeshNodeDescriptorKey(String meshName, RoutingId rid) {}

public record ZLinkClientServerServerDescriptor(
    String channelName,
    RoutingId serverRid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    int weight,
    ZLinkFrameworkRuntimeState state,
    String securityIdentity,
    String ownerId,
    long leaseGeneration,
    Instant updatedAt) {}
public record ZLinkClientServerServerDescriptorKey(
    String channelName, RoutingId serverRid) {}

public record ZLinkFanoutPublisherDescriptor(
    String channelName,
    RoutingId publisherRid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    ZLinkFrameworkRuntimeState state,
    String securityIdentity,
    String ownerId,
    long leaseGeneration,
    Instant updatedAt) {}
public record ZLinkFanoutPublisherDescriptorKey(
    String channelName, RoutingId publisherRid) {}

public record ZLinkSpotLocation(
    String meshName,
    String spotId,
    long spotGeneration,
    RoutingId ownerNodeRid,
    long ownerNodeGeneration,
    ZLinkSpotKind spotKind,
    String spotType,
    String ownerId,
    long leaseGeneration,
    Instant updatedAt) {}
public record ZLinkSpotLocationKey(String spotId) {}

public record ZLinkActorLocation(
    String meshName,
    String actorId,
    String actorType,
    ActorRef actorRef,
    RoutingId ownerNodeRid,
    long ownerNodeGeneration,
    String spotId,
    long spotGeneration,
    ZLinkSpotKind spotKind,
    String ownerId,
    long leaseGeneration,
    Instant updatedAt) {}
public record ZLinkActorLocationKey(String actorId) {}

public interface ZLinkMeshNodeLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateMeshNode(
        ZLinkMeshNodeDescriptor descriptor, ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeMeshNode(
        ZLinkMeshNodeDescriptorKey key, ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> listMeshNodes(
        String meshName, ZLinkPageRequest page);
}

public interface ZLinkClientServerLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateClientServer(
        ZLinkClientServerServerDescriptor descriptor, ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeClientServer(
        ZLinkClientServerServerDescriptorKey key, ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkLocationPage<ZLinkClientServerServerDescriptor>> listClientServers(
        String channelName, ZLinkPageRequest page);
}

public interface ZLinkFanoutLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateFanoutPublisher(
        ZLinkFanoutPublisherDescriptor descriptor, ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeFanoutPublisher(
        ZLinkFanoutPublisherDescriptorKey key, ZLinkLocationOwnerToken owner);
    CompletionStage<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>> listFanoutPublishers(
        String channelName, ZLinkPageRequest page);
}

public interface ZLinkLocationStore extends
    ZLinkMeshNodeLocationStore,
    ZLinkOwnerLeaseStore,
    ZLinkAuthorityStore {
    CompletionStage<Long> removeAllByOwner(ZLinkLocationOwnerToken owner);
}
```

`pendingCreation`은 allocation이 Pending이면 반드시 non-empty이고 Active이면 반드시 empty다. Provider는
provider-issued reservation ID와 Actor·User Spot·Instance Spot 생성 요청의 immutable content reference, exact
32-byte SHA-256과 `0..1 MiB` encoded size를 저장한다. Framework는 snapshot의 store version, generation,
owner와 allocation target을 이 projection과 결합해 exact Commit 또는 Abort fence를 복원하며, 별도
process-local reservation index나 caller-generated reservation ID에 의존하지 않는다. Target-owned Instance
Spot의 cold activation content만 complete `instance-activation-recovery-v1` envelope이며, Actor와 User Spot의
manager create content에는 이 envelope를 사용하지 않는다.

Actor manager 생성은 `completeCreation`으로 Created·Rejected·Failed를 기록하고 infrastructure recovery만
`abort`로 reservation을 정리한다. 서로 다른 operation이 같은 Creating authority를 관찰하면 authority 종료를
기다린다. Ready로 끝나면 각 요청에 Existing을 반환하고, Rejected·Failed 정리가 끝났으면 각 요청이 새
reservation을 얻어 실행한다. 이전 operation의 application reply를 공유하지 않는다. `readCreationTerminal`은
source node RID raw bytes, source lifecycle generation과 128-bit operation ID가 모두 같은 replay에만
`creation-operation-terminal-v1` semantic envelope를 반환한다. Envelope에는 transport correlation과 reply
route가 없으며 runtime은 현재 요청의 framing을 새로 만든다. Terminal은 원래 요청 deadline에서 최소 5분 유지한다.

`ZLinkLocationStore`가 MeshNode descriptor, owner lease와 generic authority capability를 함께 제공한다.
User·[Instance Spot](../../../../01-glossary.ko.md#entry-spot-user-spot과-instance-spot)은 global `SpotId`에서 파생한 하나의 authority key를 공유한다. User Spot
create와 Instance cold claim은 generic `reserve`가 같은 row의 `Missing → Pending`, generation 발급과
target pending capacity 증가를 한 transaction으로 처리한다. 성공한 activation은 같은 reservation의 `commit`으로
`Pending → Active`와 capacity 전환을 함께 공개하고 실패는 exact fence의 `abort`로 정리한다. Actor direct
resolve도 canonical authority key를 읽는다. Operational
[Spot](../../../../01-glossary.ko.md#spot)·Actor 목록은 Framework가 authority enumeration과 opaque payload를 decode한 projection이며 routing
authority가 아니다. `ZLinkSpotLocation`은 이 projection type이고 provider write·remove·resolve interface가
아니다. `ZLinkSpotLocation.spotGeneration`, `SpotRef.objectGeneration()`과
`ZLinkActorLocation.actorRef().objectGeneration()`은 provider가 반환한 `objectGeneration`을 그대로 사용한다.
Authority envelope의 `authorityOwnerGeneration`은 authority owner 이관 fence이고 descriptor·projection의
`leaseGeneration`은 host lease fence다. 두 generation을 합치거나 Framework 계산값으로 만들지 않는다.
Maintenance owner 이관은 `NEW_OWNER`로 owner generation만 바꾸고 object generation을 유지한다.
기존 ref의 ObjectGeneration은 유지되며 이전 owner route를 사용하면 runtime이 current authority를 재조회하여 forwarding
또는 retry한다. Explicit close는 Active allocation과 row를 제거한다. 이후 cold recreate는 generic
`reserve`가 새 object generation을 발급하므로 이전 ref snapshot은 영구적으로 stale다.
ClientServer와 fanout은 이 interface가 상속하지 않는 선택 capability이며 Redis provider는 두
interface를 추가로 구현한다. Host가 별도 `ZLinkAuthorityStore` instance를 등록하는 API는 없다.
`compareExchange`만 authority payload를 변경한다.
Unconditional write와 delete는 제공하지 않는다. Provider는
payload를 해석하지 않고 store clock과 current value를 같은 operation 결과로 반환한다. Authority row는
TTL을 갖지 않고 explicit fenced delete가 성공할 때까지 유지된다. Owner·coordinator lease는 별도 token row에
저장하며 lease 만료나 reclaim이 authority row를 삭제하거나 수정하지 않는다. Relocation
put과 renew의 retention은 Framework가 24시간으로 전달하며 별도 public setting을 제공하지 않는다. Current
authority reference를 확인한 coordinator만 renew를 호출하고 missing reference는
`ZLinkRelocationRenewMissing` 정상 결과다. `ZLinkRelocationRenewed`는 provider clock의 새 expiry와 Store
time을 반환하며 runtime은 local clock으로 provider expiry를 추측하지 않는다.

Framework는 logical relocation을 immutable 64 MiB chunk 최대 4096개와 root manifest로 내부에서 나누므로
logical state ceiling은 256 GiB다. `ZLinkRelocationStore`의 opaque put/get interface는 바꾸지 않으며 chunk
크기, 개수와 manifest를 설정하는 public option도 제공하지 않는다. Capture가 ceiling을 넘으면 seal을 되돌려
normal messaging을 다시 허용하고 Retire 결과를 `BLOCKED`로 종료한다. 일반 message의 negotiated effective
bound는 relocation chunk 크기 때문에 줄이지 않는다.

Missing read는 `storeNow`만 반환하고 fake StoreVersion을 갖지 않는다. `compareExchange`는
`ZLinkAuthorityExpectation`을 받는 overload만 제공한다. `PRESERVE`·`NEW_OWNER`·delete는 current
StoreVersion을 담은 `ZLinkAuthorityExpectFound`와 Active allocation을 요구한다.
`ZLinkAuthorityPut.targetOwner`는 `PRESERVE`에서 empty이고 `NEW_OWNER`에서 반드시 값을 가져야 한다.
Provider는 exact target owner lease를 CAS와 같은
transaction에서 검증하고 성공한 snapshot과 stored result의 `ownerId`·`ownerLeaseGeneration`으로 기록하며
opaque payload에서 owner metadata를 해석하지 않는다. 정상 create는 generic reservation만 사용한다.
`PRESERVE`와 delete는 stored current owner lease, `NEW_OWNER`는 `targetOwner` lease를 검증한다.
Missing·stale lease는 current authority read를 가진
`ZLinkAuthorityConflict`로 끝나고 mutation은 0이다. Invalid `targetOwner` 조합은 provider 호출 전에
`IllegalArgumentException`으로 거부한다. `relocationCapacityFence`는 `NEW_OWNER`에서 반드시 값을 갖고
`PRESERVE`에서 empty여야 한다. `NEW_OWNER` 성공은 fence의 source active 감소와 target active 증가,
target Active allocation 교체를 authority owner metadata와 같은 transaction에서 적용한다. Provider domain은 영구적인 global object generation,
authority owner generation과 Store revision counter를 각각 하나씩 유지한다. CAS 성공 operation에서
`NEW_OWNER`는 owner generation만 증가시키며 `PRESERVE`는 두 generation을 모두 유지한다. Stored mutation과
delete는 global Store revision으로 fence한다. Delete는 stored current owner lease와 Active allocation을
exact 검증하고 active capacity delta 감소와 row 제거를 한 transaction으로 적용한다. Per-key counter나
version tombstone은 유지하지 않는다.
Scan lease가 활성화된 동안만 scan snapshot을 유지하기 위한 tombstone을 bounded로 유지할 수 있다.
Authority payload에 generation을 중복 encode하지 않는다.

Global object generation, authority owner generation 또는 Store revision이 `Long.MAX_VALUE`인 상태에서
CAS가 새 값을 요구하면 provider는 `ZLinkAuthorityGenerationExhausted`를 반환한다. 이 결과는
non-retriable이며 row·index·counter를 바꾸거나 값을 소비하지 않는다. 외부 상태가 바뀌지 않은 채 같은 key와
expectation으로 다시 호출하면 같은 결과를 반환한다. Provider·transport exception 및
`ZLinkPlacementCapacityExhausted`와는 서로 다른 결과다. Framework는 이를 기존 high-level lifecycle 실패로
종료하며 application public error enum을 추가하지 않는다.

Framework가 provider operation에 넘긴 `byte[]`은 반환된 `CompletionStage`가 완료될 때까지 유효하고 내용이
바뀌지 않는다. Provider가 완료 뒤에도 payload를 보관하려면 완료 전에 복사해야 한다. 성공 결과로 반환한
`byte[]` storage는 Framework가 처리하는 동안 안정적이어야 하며 provider가 내용을 바꾸거나 재사용하지 않는다.
Mutable buffer를 사용하는 adapter는 public boundary에서 snapshot을 만든다. 이미 cancellation이 요청된
operation은 provider를 호출하지 않고 I/O와 commit을 수행하지 않는다. Operation이 시작된 뒤 waiter
cancellation이나 error가 발생하면 commit 여부는 unknown이며 no-commit을 보장하지 않는다. Authority CAS는
같은 key와 expected StoreVersion을 exact read해 결과를 reconcile한 뒤 필요하면 retry한다. Content-addressed
relocation put은 같은 content를 read·verify한 뒤 retry한다. Authority에 연결되지 않은 committed put은 orphan으로
retention까지 유지한 뒤 cleanup한다. 이 동작을 위한 public result는 추가하지 않는다.

모든 cross-node Actor·Spot 이동은 Relocation Store를 사용한다. `RECREATE`도 accepted journal과 recovery payload를
저장하고 `SNAPSHOT`은 application state를 추가한다. Same-node Actor join에는 Relocation payload를 만들지 않으며
`DISABLED` cross-node 이동은 capture 전에 거부한다. Runtime은 immutable root와 manifest를 먼저 저장하고
reference·checksum·retention을 검증한 뒤 Location authority CAS 한 번으로 publish한다. Aggregate prepare의
`participants`는 [Location Store](../../../../01-glossary.ko.md#location-store)가 소유하는 bounded canonical participant set이고 `inventoryDigest`는 participant별
mutation까지 포함한 exact 32-byte SHA-256이다. Relocation manifest의 inventory는 payload lookup projection일 뿐
authority가 아니며 두 digest가 일치해야 restore와 replay를 시작한다.
Aggregate prepare는 participant의 `ownerTransition`으로 두 mode를 판정한다. `NEW_OWNER`가 하나라도 있는
relocation mode는 `PRESERVE` participant와 섞을 수 있지만 non-zero capacity bundle과 Reserved fence는
`NEW_OWNER` participant의 durable allocation delta만 exact 합산하고 일대일 대응해야 한다. 각 fence를
`(aggregateId, aggregateGeneration)`에 원자적으로 연결하고 Prepared로 전환한다. 연결된 fence를
`abortRelocationCapacity`로 직접 정리하면 `STALE`이고 다른 aggregate가 같은 fence를 prepare하면 mutation 없이
`ZLinkAggregateConflict`다.

모든 participant가 `PRESERVE`이면 completion·steady-normalization mode다. Capacity는 exact zero이고 모든
membership mutation은 empty여야 한다. Capacity reservation 없이 exact participant set의 authority payload만
atomic하게 변경하며 owner, object generation, authority owner generation과 durable Active allocation을 유지한다.
Zero capacity와 `NEW_OWNER`, non-zero capacity와 all-Preserve 조합은 `ZLinkAggregateConflict`이고 mutation은
0이다. Participant payload·membership mutation, inventory digest, target owner와 mode별 fence 목록까지 정확히
같은 duplicate만 `ZLinkAggregateAlreadyPrepared`다. `commitAggregate`와 `abortAggregate`만 relocation mode의
연결된 fence를 최종 확정하거나 해제한다. Commit 직전 mode별 expectation이 stale이면 authority와 fence binding을
바꾸지 않고 `STALE`을 반환한다.

`RECREATE` 또는 `SNAPSHOT` factory가 하나라도 있거나 Instance Spot factory가 하나라도 있는 host는
`ZLinkRelocationStore`를 정확히 하나 등록한다. Instance Spot factory가 없고 모든 factory가 `DISABLED`인
same-node 구성만 이를 생략할 수 있다.

CAS conflict 전에 저장된 root는 orphan retention으로 제거한다. Root 교체는 new root 저장, Location reference CAS,
old root cleanup 순서이며 삭제는 Location reference release CAS 뒤 Relocation delete 순서다. 두 Store 사이의 transaction이나
2PC는 요구하지 않는다. Published reference가 permanent missing이거나 checksum·inventory digest가 다르면
non-retriable `RelocationDataLost`로 seal하며 이전 owner로 rollback하지 않는다.

Framework는 host process lifecycle마다 새 owner ID를 만들고 application이 owner ID를 설정하거나
이전 lifecycle의 값을 재사용하는 API를 제공하지 않는다. 한 host의 모든
MeshNode·ClientServer·fanout descriptor와 authority는 같은 host token을 참조하고 각 descriptor가
자신의 RID를 갖는다. Provider domain은 영구적인 global lease generation counter 하나를 유지하고
claim이 성공할 때마다 증가시켜 1부터 `Long.MAX_VALUE`까지의 token을 발급한다. Active owner ID의
중복 claim은 `ZLinkOwnerLeaseClaimConflict`다. Expiry·release는 active row를 삭제하며 같은 owner ID로
다시 claim하면 더 큰 global generation을 받는다. 지연된 renew·release는 `STALE`로 거부한다.
Target admission 직전에 `readOwnerLease(ownerId)`로 exact token을 다시 확인한다. Owner lease 전체 목록과
snapshot type은 public surface에 제공하지 않는다.

Global lease generation counter가 `Long.MAX_VALUE`에 도달한 뒤 새 generation이 필요한 claim은
`ZLinkOwnerLeaseGenerationExhausted`를 반환한다. 만료된 row를 takeover하는 claim도 같은 규칙을 적용한다.
이 결과는 provider exception이나 retriable conflict가 아니며 row·index·counter를 바꾸거나 값을 소비하지
않는다. 같은 expectation을 다시 호출하면 외부 상태가 바뀌지 않는 한 같은 결과를 반환한다. Renew와 release는
새 generation이 필요하지 않으므로 이 결과를 추가하지 않는다.

Descriptor와 peer enumeration은 `ZLinkPageRequest`와 `ZLinkLocationPage<T>`를 사용한다. `firstPage()`의
effective page size는 100이고 명시한 `pageSize`는 `1..1000`이어야 한다. Continuation token은 provider만
해석하는 opaque value다. Provider는 encoded page가 4 MiB에 먼저 도달하면 요청보다 적은 item과 다음 token을
반환하며 byte limit public option은 제공하지 않는다. Framework reconciler는 scope change stamp를 읽고 모든
page를 조립한 뒤 stamp를 다시 읽는다. 두 stamp가 같을 때만 full snapshot을 적용하고 다르면 부분 결과를
버리고 first page부터 다시 읽는다. Page 조립과 retry는 Framework 내부 동작이다.
Authority scan의 first page는 empty cursor로 요청한다. Provider는 한 snapshot을 만들고 이어지는 page에
필요한 모든 상태를 하나의 `ZLinkAuthorityScanCursor`에 담는다. 다음 page는 직전 page의 `nextCursor`
객체를 해석하거나 다시 조립하지 않고 그대로 넘긴다. Cursor의 UTF-8 encoded 크기는 `1..4096` bytes이며
empty cursor는 허용하지 않는다. Record constructor는 이 범위를 검증하며 `String` 값은 만든 뒤 바뀌지
않는다. Provider는 snapshot에 포함된 key incarnation을 scan 전체에서 각각 한 번만 반환한다. Concurrent
delete는 Framework의 exact read에서 missing으로 제거되고 snapshot 뒤의 create·recreate는 다음 scan에서
반환된다. Framework는 각 candidate를 exact read한 뒤 current StoreVersion으로 CAS한다. 등록한
MeshName scope의 initial scan이 완료되기 전에는 Serving을 게시하지 않고 이후 scan은 background로
반복한다.
Provider가 cursor가 가리키는 scan을 만료시켰으면 이어지는 page 요청은 `ZLinkAuthorityScanExpired`를
반환한다. Framework는 부분 결과를 사용하지 않고 first page부터 새 scan을 시작한다.

한 authority opaque payload의 encoded 크기는 최대 1 MiB다. Scan `limit`은 `1..1000`이고 provider는 encoded
page 4 MiB에 먼저 도달하면 요청보다 적은 entry와 `nextCursor`를 반환한다. 이 byte limit을 바꾸는
public option은 없다. Hot authority row는 compact metadata와 replay cursor만 보관하며 complete terminal reply
bytes는 relocation stream에 저장한다.

MeshNode의 `objectCapabilities`는 Actor·User Spot·Instance Spot을 object kind와 type별로 구분하고 maintenance
policy, Snapshot adapter 등록 여부와 type별 capacity limit을 같은 항목에 둔다.
`hasSnapshotAdapter`는 target에 해당 object kind의 adapter가 등록되어 있는지만 나타내며 application state의
format, version이나 contract ID를 광고하지 않는다.

[MeshNode descriptor](../../../../01-glossary.ko.md#meshnode-descriptor)는 `ZLinkFrameworkRuntimeState` 하나로 lifecycle 상태를 나타낸다. 별도 `draining`
boolean을 두지 않으므로 서로 모순되는 상태 조합을 만들 수 없다.

Descriptor의 key, RID, lifecycle generation, endpoint, security identity, owner token, application version,
ChannelName key set, Spot type set와 object capability의 kind·type·policy·Snapshot adapter 등록 여부·limit은
첫 admission 뒤 해당 lifecycle에서 바뀌지 않는다. Channel weight 값, capability capacity, maintenance wave와 runtime state만
mutable하다. Mutable update는 current owner token과 같은 [lifecycle generation](../../../../01-glossary.ko.md#lifecycle-generation)을 제시하고
`descriptorRevision`을 strictly 증가시켜야 한다. Provider는 stale revision이나 immutable field 변경을 원자적으로
거부하며 일부 field만 적용하지 않는다. ClientServer와 fanout descriptor도 같은 identity·revision fence를
적용한다.

`lifecycleGeneration`은 0이 아닌 opaque equality token이다. Runtime은 수치 크기로 lifecycle의 선후를
판정하지 않는다. Store-backed descriptor에는 exact owner lease·descriptor lifetime token을 사용한다. Manual
descriptor에는 runtime이 CSPRNG로 만든 nonce를 사용하고 current connection handover fence와 함께 검증한다.
Application이 값을 선택하는 option은 없다. 순서를 비교하는 값은 `descriptorRevision`뿐이다. 이 revision이
`Long.MAX_VALUE`인 상태에서 다음 값이 필요하면 host를 `ERROR`로 seal하고 wrap하지 않는다. Runtime은
lifetime token의 source를 application callback에 노출하지 않는다.

JVM runtime은 owner와 relocation을 같은 authority row에서 `Preparing`, `Captured`, `Prepared`, `Committed`,
`Activating`, `Activated`, `Cleaning`, `Completed`, `Aborted` phase로 진행한다. 별도 actor-relocation row나
application이 조립하는 owner token을 만들지 않는다. Location owner lease와 service liveness는 서로 다른
계약이다.

`reserve`, `commit`, `abort`는 Actor, User Spot과 Instance Spot에 공통인 generic object reservation이다.
Request는 object kind, global authority key, stable type, creation content
reference·exact 32-byte SHA-256·encoded size, target descriptor, exact owner token과 pending capacity delta를
받는다. Target descriptor key와 0보다 큰 lifecycle generation은 request와 reservation에 함께 고정한다.
Capacity delta의 유효 범위는 `1..Integer.MAX_VALUE`다. Reserve request는 Framework가 encode한 opaque Pending
authority payload도 받고, commit은 opaque Active
authority payload를 받는다. Provider는 두 payload를 해석하거나 합성하지 않고 해당 authority revision에 그대로
기록한다. Reserve는 authority row에 object kind·[stable type](../../../../01-glossary.ko.md#stable-type)·target descriptor key·lifecycle generation·capacity
delta를 포함한 Pending allocation을 저장한다. Commit은 reservation을 exact 검증하고 target descriptor와 owner
lease가 여전히 current일 때만 Pending allocation을 Active로 전환한다. Abort는 exact reservation과 Pending
allocation만 검증하며 current descriptor·lease liveness와 관계없이 row와 pending delta를 정리한다.
Encoded request는 최대 1 MiB다. Reserve는 kind·type mismatch와 capacity exhaustion을 closed result로
구분하고, commit·abort는 같은 reservation에 idempotent하며 다른 fence를 stale로 거부한다. Aggregate operation은
0이 아닌 128-bit ID, 최대 1024 participant와 encoded 1 MiB bound를 적용하고 owner·[membership](../../../../01-glossary.ko.md#membership) visibility를
하나의 commit generation으로 전환한다.

Existing object relocation은 creation reservation을 재사용하지 않는다. `reserveRelocationCapacity`는 zero가 아닌
UUID, current authority version, kind·stable type, source·target descriptor lifecycle과 exact owner token을
검증하고 target capacity를 예약한다. Source owner lease·descriptor liveness는 요구하지 않고, current Active
allocation과 source field가 정확히 같은지만 검증한다. Target descriptor와 owner lease는 reserve, aggregate
prepare와 commit 직전에 다시 검증한다. 같은 ID와 exact request는
`ZLinkRelocationCapacityAlreadyReserved`, 다른 내용은
conflict다. Standalone Actor의 `NEW_OWNER` CAS와 aggregate commit만 fence를 소비하며 source active 감소와 target
active 증가와 Active allocation 교체를 authority mutation과 같은 transaction에서 처리한다. Commit 전
`abortRelocationCapacity`는 pending을 해제하고 reservation은 TTL로 만료시키지 않는다.
Standalone `NEW_OWNER`에 전달한 fence가 reserved 상태가 아니거나 authority key, expected StoreVersion,
source owner, target owner 중 하나라도 reservation과 다르면 current authority read를 담은
`ZLinkAuthorityConflict`를 반환한다. 이미 committed·aborted된 fence도 같은 결과이며 authority row,
capacity와 fence 상태는 바뀌지 않는다.

## Exact public member inventory

아래 선언은 이 category의 Java public type과 member를 고정한다.

```java
public final class systems.zlink.framework.locations.ZLinkLocationOptions {
  public systems.zlink.framework.locations.ZLinkLocationOptions();
  public java.time.Duration ownerLeaseRenewInterval();
  public void setOwnerLeaseRenewInterval(java.time.Duration);
  public java.time.Duration ownerLeaseTtl();
  public void setOwnerLeaseTtl(java.time.Duration);
  public java.time.Duration pollingInterval();
  public void setPollingInterval(java.time.Duration);
  public java.time.Duration storeFailureGrace();
  public void setStoreFailureGrace(java.time.Duration);
  public java.time.Duration ownerLeaseFencingMargin();
  public void setOwnerLeaseFencingMargin(java.time.Duration);
  public java.time.Duration ownerLeaseRenewTimeout();
  public void setOwnerLeaseRenewTimeout(java.time.Duration);
  public java.time.Duration routeCacheMaxAge();
  public void setRouteCacheMaxAge(java.time.Duration);
  public java.time.Duration relocationForwardingWindow();
  public void setRelocationForwardingWindow(java.time.Duration);
  public int maxActiveOutboundRelocations();
  public void setMaxActiveOutboundRelocations(int);
  public int maxActiveInboundRelocations();
  public void setMaxActiveInboundRelocations(int);
  public int maxConcurrentRelocationCaptures();
  public void setMaxConcurrentRelocationCaptures(int);
  public int maxConcurrentRelocationRestores();
  public void setMaxConcurrentRelocationRestores(int);
  public long maxRelocationPayloadInFlightBytes();
  public void setMaxRelocationPayloadInFlightBytes(long);
}
public final class systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions {
  public systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions();
  public java.lang.String connectionString();
  public systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions setConnectionString(java.lang.String);
  public java.lang.String keyPrefix();
  public systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions setKeyPrefix(java.lang.String);
  public java.time.Duration commandTimeout();
  public systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions setCommandTimeout(java.time.Duration);
}
public final class systems.zlink.framework.locations.redis.ZLinkRedisRelocationOptions {
  public systems.zlink.framework.locations.redis.ZLinkRedisRelocationOptions();
  public java.lang.String connectionString();
  public systems.zlink.framework.locations.redis.ZLinkRedisRelocationOptions setConnectionString(java.lang.String);
  public java.lang.String keyPrefix();
  public systems.zlink.framework.locations.redis.ZLinkRedisRelocationOptions setKeyPrefix(java.lang.String);
  public java.time.Duration commandTimeout();
  public systems.zlink.framework.locations.redis.ZLinkRedisRelocationOptions setCommandTimeout(java.time.Duration);
}
public final class systems.zlink.framework.locations.ZLinkPlacementObjectKind extends java.lang.Enum<systems.zlink.framework.locations.ZLinkPlacementObjectKind> {
  public static final systems.zlink.framework.locations.ZLinkPlacementObjectKind ACTOR;
  public static final systems.zlink.framework.locations.ZLinkPlacementObjectKind USER_SPOT;
  public static final systems.zlink.framework.locations.ZLinkPlacementObjectKind INSTANCE_SPOT;
  public static systems.zlink.framework.locations.ZLinkPlacementObjectKind[] values();
  public static systems.zlink.framework.locations.ZLinkPlacementObjectKind valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkMeshNodeObjectRole extends java.lang.Enum<systems.zlink.framework.locations.ZLinkMeshNodeObjectRole> {
  public static final systems.zlink.framework.locations.ZLinkMeshNodeObjectRole NONE;
  public static final systems.zlink.framework.locations.ZLinkMeshNodeObjectRole CLIENT;
  public static final systems.zlink.framework.locations.ZLinkMeshNodeObjectRole SERVER;
  public static systems.zlink.framework.locations.ZLinkMeshNodeObjectRole[] values();
  public static systems.zlink.framework.locations.ZLinkMeshNodeObjectRole valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind extends java.lang.Enum<systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind> {
  public static final systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind DISABLED;
  public static final systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind RECREATE;
  public static final systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind SNAPSHOT;
  public static systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind[] values();
  public static systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkObjectCapability extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkObjectCapability(systems.zlink.framework.locations.ZLinkPlacementObjectKind, java.lang.String, systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind, boolean, java.util.Set<java.lang.String>, int);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkPlacementObjectKind objectKind();
  public java.lang.String stableType();
  public systems.zlink.framework.locations.ZLinkObjectMaintenancePolicyKind policy();
  public boolean hasSnapshotAdapter();
  public int spotLimit();
}
public final class systems.zlink.framework.locations.ZLinkCapacityUsage extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkCapacityUsage(int, int, int);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public int active();
  public int reserved();
  public int limit();
}
public final class systems.zlink.framework.locations.ZLinkSpotTypeCapacity extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkSpotTypeCapacity(systems.zlink.framework.locations.ZLinkPlacementObjectKind, java.lang.String, systems.zlink.framework.locations.ZLinkCapacityUsage);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkPlacementObjectKind objectKind();
  public java.lang.String stableType();
  public systems.zlink.framework.locations.ZLinkCapacityUsage usage();
}
public final class systems.zlink.framework.locations.ZLinkPlacementCapacity extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkPlacementCapacity(systems.zlink.framework.locations.ZLinkCapacityUsage, systems.zlink.framework.locations.ZLinkCapacityUsage, java.util.List<systems.zlink.framework.locations.ZLinkSpotTypeCapacity>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkCapacityUsage actors();
  public systems.zlink.framework.locations.ZLinkCapacityUsage spots();
  public java.util.List<systems.zlink.framework.locations.ZLinkSpotTypeCapacity> spotTypes();
}
public final class systems.zlink.framework.locations.ZLinkObjectReservationRequest extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkObjectReservationRequest(systems.zlink.framework.locations.ZLinkPlacementObjectKind, java.lang.String, java.lang.String, java.lang.String, byte[], int, systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, long, systems.zlink.framework.locations.ZLinkLocationOwnerToken, byte[], systems.zlink.framework.locations.ZLinkPlacementCapacityBundle);
}
public final class systems.zlink.framework.locations.ZLinkObjectReservation extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkObjectReservation(java.lang.String, java.lang.String, long, long, java.lang.String, systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, long, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
}
public sealed interface systems.zlink.framework.locations.ZLinkObjectReserveResult permits systems.zlink.framework.locations.ZLinkObjectReserved, systems.zlink.framework.locations.ZLinkObjectConflict, systems.zlink.framework.locations.ZLinkObjectAlreadyExists, systems.zlink.framework.locations.ZLinkObjectTypeMismatch, systems.zlink.framework.locations.ZLinkPlacementCapacityExhausted, systems.zlink.framework.locations.ZLinkObjectGenerationExhausted {
}
public final class systems.zlink.framework.locations.ZLinkObjectCommitResult extends java.lang.Enum<systems.zlink.framework.locations.ZLinkObjectCommitResult> {
  public static final systems.zlink.framework.locations.ZLinkObjectCommitResult COMMITTED;
  public static final systems.zlink.framework.locations.ZLinkObjectCommitResult ALREADY_COMMITTED;
  public static final systems.zlink.framework.locations.ZLinkObjectCommitResult STALE;
  public static final systems.zlink.framework.locations.ZLinkObjectCommitResult GENERATION_EXHAUSTED;
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkObjectAbortResult extends java.lang.Enum<systems.zlink.framework.locations.ZLinkObjectAbortResult> {
  public static final systems.zlink.framework.locations.ZLinkObjectAbortResult ABORTED;
  public static final systems.zlink.framework.locations.ZLinkObjectAbortResult ALREADY_ABORTED;
  public static final systems.zlink.framework.locations.ZLinkObjectAbortResult STALE;
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkMeshNodeDescriptor extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkMeshNodeDescriptor(java.lang.String, systems.zlink.contracts.core.RoutingId, long, long, java.lang.String, java.util.Map<java.lang.String, java.lang.Integer>, long, java.util.List<systems.zlink.framework.locations.ZLinkObjectCapability>, systems.zlink.framework.locations.ZLinkMeshNodeObjectRole, int, systems.zlink.framework.locations.ZLinkPlacementCapacity, java.util.Optional<java.lang.String>, systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState, java.lang.String, java.lang.String, long, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String meshName();
  public systems.zlink.contracts.core.RoutingId rid();
  public long lifecycleGeneration();
  public long descriptorRevision();
  public java.lang.String endpoint();
  public java.util.Map<java.lang.String, java.lang.Integer> channelWeights();
  public long applicationVersion();
  public java.util.List<systems.zlink.framework.locations.ZLinkObjectCapability> objectCapabilities();
  public systems.zlink.framework.locations.ZLinkMeshNodeObjectRole objectRole();
  public int placementWeight();
  public systems.zlink.framework.locations.ZLinkPlacementCapacity capacity();
  public java.util.Optional<java.lang.String> maintenanceWave();
  public systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState state();
  public java.lang.String securityIdentity();
  public java.lang.String ownerId();
  public long leaseGeneration();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey(java.lang.String, systems.zlink.contracts.core.RoutingId);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String meshName();
  public systems.zlink.contracts.core.RoutingId rid();
}
public final class systems.zlink.framework.locations.ZLinkClientServerServerDescriptor extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkClientServerServerDescriptor(java.lang.String, systems.zlink.contracts.core.RoutingId, long, long, java.lang.String, int, systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState, java.lang.String, java.lang.String, long, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String channelName();
  public systems.zlink.contracts.core.RoutingId serverRid();
  public long lifecycleGeneration();
  public long descriptorRevision();
  public java.lang.String endpoint();
  public int weight();
  public systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState state();
  public java.lang.String securityIdentity();
  public java.lang.String ownerId();
  public long leaseGeneration();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey(java.lang.String, systems.zlink.contracts.core.RoutingId);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String channelName();
  public systems.zlink.contracts.core.RoutingId serverRid();
}
public final class systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor(java.lang.String, systems.zlink.contracts.core.RoutingId, long, long, java.lang.String, systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState, java.lang.String, java.lang.String, long, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String channelName();
  public systems.zlink.contracts.core.RoutingId publisherRid();
  public long lifecycleGeneration();
  public long descriptorRevision();
  public java.lang.String endpoint();
  public systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState state();
  public java.lang.String securityIdentity();
  public java.lang.String ownerId();
  public long leaseGeneration();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey(java.lang.String, systems.zlink.contracts.core.RoutingId);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String channelName();
  public systems.zlink.contracts.core.RoutingId publisherRid();
}
public interface systems.zlink.framework.locations.ZLinkMeshNodeLocationStore {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkMeshNodeDescriptor>> listMeshNodes(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
}
public interface systems.zlink.framework.locations.ZLinkClientServerLocationStore {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateClientServer(systems.zlink.framework.locations.ZLinkClientServerServerDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeClientServer(systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkClientServerServerDescriptor>> listClientServers(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
}
public interface systems.zlink.framework.locations.ZLinkFanoutLocationStore {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor>> listFanoutPublishers(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
}
public final class systems.zlink.framework.locations.redis.ZLinkRedisLocationStore implements systems.zlink.framework.locations.ZLinkLocationStore, systems.zlink.framework.locations.ZLinkClientServerLocationStore, systems.zlink.framework.locations.ZLinkFanoutLocationStore, systems.zlink.framework.locations.ZLinkPeerLocationStore, systems.zlink.framework.locations.ZLinkRouteLocationStore, systems.zlink.framework.locations.ZLinkLocationChangeStampStore, java.lang.AutoCloseable {
  public systems.zlink.framework.locations.redis.ZLinkRedisLocationStore(systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkMeshNodeDescriptor>> listMeshNodes(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateClientServer(systems.zlink.framework.locations.ZLinkClientServerServerDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeClientServer(systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkClientServerServerDescriptor>> listClientServers(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor>> listFanoutPublishers(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updatePeer(systems.zlink.framework.locations.ZLinkPeerLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> removePeer(systems.zlink.framework.locations.ZLinkPeerLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkPeerLocation>> listPeerLocations(systems.zlink.framework.locations.ZLinkPeerLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateRoute(systems.zlink.framework.locations.ZLinkRouteLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> removeRoute(systems.zlink.framework.locations.ZLinkRouteLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRouteLocation> resolveRoute(systems.zlink.framework.locations.ZLinkRouteLocationKey);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkRouteLocation>> listRouteLocations(systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult> claimOwnerLease(java.lang.String, java.time.Duration);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult> readOwnerLease(java.lang.String);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult> renewOwnerLease(systems.zlink.framework.locations.ZLinkLocationOwnerToken, java.time.Duration);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult> releaseOwnerLease(systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public java.util.concurrent.CompletionStage<java.lang.Long> removeAllByOwner(systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityReadResult> read(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityWriteResult> compareExchange(java.lang.String, systems.zlink.framework.locations.ZLinkAuthorityExpectation, systems.zlink.framework.locations.ZLinkAuthorityMutation, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityScanResult> list(java.lang.String, java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor>, int, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectReserveResult> reserve(systems.zlink.framework.locations.ZLinkObjectReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectCommitResult> commit(systems.zlink.framework.locations.ZLinkObjectReservation, byte[], systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectAbortResult> abort(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult> reserveRelocationCapacity(systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult> abortRelocationCapacity(systems.zlink.framework.locations.ZLinkRelocationCapacityFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregatePrepareResult> prepareAggregate(systems.zlink.framework.locations.ZLinkAggregatePrepareRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregateCommitResult> commitAggregate(systems.zlink.framework.locations.ZLinkAggregateFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregateAbortResult> abortAggregate(systems.zlink.framework.locations.ZLinkAggregateFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<java.lang.Long> getChangeStamp(systems.zlink.framework.locations.ZLinkLocationChangeStampScope);
  public void close();
}
public final class systems.zlink.framework.locations.redis.ZLinkRedisRelocationStore implements systems.zlink.framework.locations.ZLinkRelocationStore, java.lang.AutoCloseable {
  public systems.zlink.framework.locations.redis.ZLinkRedisRelocationStore(systems.zlink.framework.locations.redis.ZLinkRedisRelocationOptions);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationStored> put(byte[], java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationReadResult> get(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationRenewResult> renew(java.lang.String, java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationDeleteResult> delete(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public void close();
}
public interface systems.zlink.framework.locations.ZLinkStoreCancellation {
  public abstract boolean isCancellationRequested();
}
public sealed interface systems.zlink.framework.locations.ZLinkAuthorityReadResult
    permits systems.zlink.framework.locations.ZLinkAuthorityMissing,
            systems.zlink.framework.locations.ZLinkAuthoritySnapshot {
}
public final class systems.zlink.framework.locations.ZLinkAuthorityMissing extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityReadResult {
  public systems.zlink.framework.locations.ZLinkAuthorityMissing(java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.time.Instant storeNow();
}
public final class systems.zlink.framework.locations.ZLinkAuthoritySnapshot extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityReadResult {
  public systems.zlink.framework.locations.ZLinkAuthoritySnapshot(java.lang.String, byte[], long, long, java.lang.String, long, systems.zlink.framework.locations.ZLinkPlacementAllocation, java.util.Optional<systems.zlink.framework.locations.ZLinkPendingObjectCreation>, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String storeVersion();
  public byte[] payload();
  public long objectGeneration();
  public long authorityOwnerGeneration();
  public java.lang.String ownerId();
  public long ownerLeaseGeneration();
  public systems.zlink.framework.locations.ZLinkPlacementAllocation allocation();
  public java.util.Optional<systems.zlink.framework.locations.ZLinkPendingObjectCreation> pendingCreation();
  public java.time.Instant storeNow();
}
public final class systems.zlink.framework.locations.ZLinkPendingObjectCreation extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkPendingObjectCreation(java.lang.String, java.lang.String, byte[], int);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String reservationId();
  public java.lang.String requestContentReference();
  public byte[] requestSha256();
  public int requestEncodedSize();
}
public final class systems.zlink.framework.locations.ZLinkPlacementAllocationState extends java.lang.Enum<systems.zlink.framework.locations.ZLinkPlacementAllocationState> {
  public static final systems.zlink.framework.locations.ZLinkPlacementAllocationState RESERVED;
  public static final systems.zlink.framework.locations.ZLinkPlacementAllocationState ACTIVE;
  public static systems.zlink.framework.locations.ZLinkPlacementAllocationState[] values();
  public static systems.zlink.framework.locations.ZLinkPlacementAllocationState valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkSpotTypeCapacityDelta extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkSpotTypeCapacityDelta(systems.zlink.framework.locations.ZLinkPlacementObjectKind, java.lang.String, int);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkPlacementObjectKind objectKind();
  public java.lang.String stableType();
  public int slots();
}
public final class systems.zlink.framework.locations.ZLinkPlacementCapacityBundle extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkPlacementCapacityBundle(int, int, java.util.Optional<systems.zlink.framework.locations.ZLinkSpotTypeCapacityDelta>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public int actorSlots();
  public int spotSlots();
  public java.util.Optional<systems.zlink.framework.locations.ZLinkSpotTypeCapacityDelta> spotType();
}
public final class systems.zlink.framework.locations.ZLinkPlacementAllocation extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkPlacementAllocation(systems.zlink.framework.locations.ZLinkPlacementAllocationState, systems.zlink.framework.locations.ZLinkPlacementObjectKind, java.lang.String, systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, long, systems.zlink.framework.locations.ZLinkPlacementCapacityBundle);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkPlacementAllocationState state();
  public systems.zlink.framework.locations.ZLinkPlacementObjectKind objectKind();
  public java.lang.String stableType();
  public systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey descriptor();
  public long descriptorLifecycleGeneration();
  public systems.zlink.framework.locations.ZLinkPlacementCapacityBundle capacityBundle();
}
public final class systems.zlink.framework.locations.ZLinkAuthorityEntry extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkAuthorityEntry(java.lang.String, systems.zlink.framework.locations.ZLinkAuthoritySnapshot);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String key();
  public systems.zlink.framework.locations.ZLinkAuthoritySnapshot snapshot();
}
public final class systems.zlink.framework.locations.ZLinkAuthorityScanCursor extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkAuthorityScanCursor(java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String encoded();
}
public sealed interface systems.zlink.framework.locations.ZLinkAuthorityScanResult
    permits systems.zlink.framework.locations.ZLinkAuthorityPage,
            systems.zlink.framework.locations.ZLinkAuthorityScanExpired {
}
public final class systems.zlink.framework.locations.ZLinkAuthorityPage extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityScanResult {
  public systems.zlink.framework.locations.ZLinkAuthorityPage(java.util.List<systems.zlink.framework.locations.ZLinkAuthorityEntry>, java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.util.List<systems.zlink.framework.locations.ZLinkAuthorityEntry> items();
  public java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor> nextCursor();
}
public final class systems.zlink.framework.locations.ZLinkAuthorityScanExpired extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityScanResult {
  public systems.zlink.framework.locations.ZLinkAuthorityScanExpired();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public sealed interface systems.zlink.framework.locations.ZLinkAuthorityExpectation
    permits systems.zlink.framework.locations.ZLinkAuthorityExpectFound {
}
public final class systems.zlink.framework.locations.ZLinkAuthorityExpectFound extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityExpectation {
  public systems.zlink.framework.locations.ZLinkAuthorityExpectFound(java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String storeVersion();
}
public sealed interface systems.zlink.framework.locations.ZLinkAuthorityMutation
    permits systems.zlink.framework.locations.ZLinkAuthorityPut,
            systems.zlink.framework.locations.ZLinkAuthorityDelete {
}
public final class systems.zlink.framework.locations.ZLinkAuthorityGenerationTransition extends java.lang.Enum<systems.zlink.framework.locations.ZLinkAuthorityGenerationTransition> {
  public static final systems.zlink.framework.locations.ZLinkAuthorityGenerationTransition PRESERVE;
  public static final systems.zlink.framework.locations.ZLinkAuthorityGenerationTransition NEW_OWNER;
  public static systems.zlink.framework.locations.ZLinkAuthorityGenerationTransition[] values();
  public static systems.zlink.framework.locations.ZLinkAuthorityGenerationTransition valueOf(java.lang.String);
}
public final class systems.zlink.framework.locations.ZLinkAuthorityPut extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityMutation {
  public systems.zlink.framework.locations.ZLinkAuthorityPut(byte[], systems.zlink.framework.locations.ZLinkAuthorityGenerationTransition, java.util.Optional<systems.zlink.framework.locations.ZLinkLocationOwnerToken>, java.util.Optional<systems.zlink.framework.locations.ZLinkRelocationCapacityFence>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public byte[] payload();
  public systems.zlink.framework.locations.ZLinkAuthorityGenerationTransition generationTransition();
  public java.util.Optional<systems.zlink.framework.locations.ZLinkLocationOwnerToken> targetOwner();
  public java.util.Optional<systems.zlink.framework.locations.ZLinkRelocationCapacityFence> relocationCapacityFence();
}
public final class systems.zlink.framework.locations.ZLinkRelocationCapacityFence extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkRelocationCapacityFence(java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String value();
}
public final class systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest(java.util.UUID, java.lang.String, java.lang.String, systems.zlink.framework.locations.ZLinkPlacementObjectKind, java.lang.String, systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, long, systems.zlink.framework.locations.ZLinkLocationOwnerToken, systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, long, systems.zlink.framework.locations.ZLinkLocationOwnerToken, systems.zlink.framework.locations.ZLinkPlacementCapacityBundle);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.util.UUID reservationId();
  public java.lang.String authorityKey();
  public java.lang.String expectedStoreVersion();
  public systems.zlink.framework.locations.ZLinkPlacementObjectKind objectKind();
  public java.lang.String stableType();
  public systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey sourceDescriptor();
  public long sourceDescriptorLifecycleGeneration();
  public systems.zlink.framework.locations.ZLinkLocationOwnerToken sourceOwner();
  public systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey targetDescriptor();
  public long targetDescriptorLifecycleGeneration();
  public systems.zlink.framework.locations.ZLinkLocationOwnerToken targetOwner();
  public systems.zlink.framework.locations.ZLinkPlacementCapacityBundle capacityBundle();
}
public sealed interface systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult
    permits systems.zlink.framework.locations.ZLinkRelocationCapacityReserved,
            systems.zlink.framework.locations.ZLinkRelocationCapacityAlreadyReserved,
            systems.zlink.framework.locations.ZLinkRelocationCapacityConflict,
            systems.zlink.framework.locations.ZLinkRelocationCapacityTargetUnavailable,
            systems.zlink.framework.locations.ZLinkRelocationCapacityExhausted {
}
public final class systems.zlink.framework.locations.ZLinkRelocationCapacityReserved extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult {
  public systems.zlink.framework.locations.ZLinkRelocationCapacityReserved(systems.zlink.framework.locations.ZLinkRelocationCapacityFence);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkRelocationCapacityFence fence();
}
public final class systems.zlink.framework.locations.ZLinkRelocationCapacityAlreadyReserved extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult {
  public systems.zlink.framework.locations.ZLinkRelocationCapacityAlreadyReserved(systems.zlink.framework.locations.ZLinkRelocationCapacityFence);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkRelocationCapacityFence fence();
}
public final class systems.zlink.framework.locations.ZLinkRelocationCapacityConflict extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult {
  public systems.zlink.framework.locations.ZLinkRelocationCapacityConflict(systems.zlink.framework.locations.ZLinkAuthorityReadResult);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkAuthorityReadResult current();
}
public final class systems.zlink.framework.locations.ZLinkRelocationCapacityTargetUnavailable extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult {
  public systems.zlink.framework.locations.ZLinkRelocationCapacityTargetUnavailable();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.locations.ZLinkRelocationCapacityExhausted extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult {
  public systems.zlink.framework.locations.ZLinkRelocationCapacityExhausted();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult extends java.lang.Enum<systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult> {
  public static final systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult ABORTED;
  public static final systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult ALREADY_ABORTED;
  public static final systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult ALREADY_COMMITTED;
  public static final systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult STALE;
  public static systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult[] values();
  public static systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult valueOf(java.lang.String);
}
public final class systems.zlink.framework.locations.ZLinkAuthorityDelete extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityMutation {
  public systems.zlink.framework.locations.ZLinkAuthorityDelete();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public sealed interface systems.zlink.framework.locations.ZLinkAuthorityWriteResult
    permits systems.zlink.framework.locations.ZLinkAuthorityStored,
            systems.zlink.framework.locations.ZLinkAuthorityDeleted,
            systems.zlink.framework.locations.ZLinkAuthorityConflict,
            systems.zlink.framework.locations.ZLinkAuthorityGenerationExhausted {
}
public final class systems.zlink.framework.locations.ZLinkAuthorityStored extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityWriteResult {
  public systems.zlink.framework.locations.ZLinkAuthorityStored(java.lang.String, byte[], long, long, java.lang.String, long, systems.zlink.framework.locations.ZLinkPlacementAllocation, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String storeVersion();
  public byte[] payload();
  public long objectGeneration();
  public long authorityOwnerGeneration();
  public java.lang.String ownerId();
  public long ownerLeaseGeneration();
  public systems.zlink.framework.locations.ZLinkPlacementAllocation allocation();
  public java.time.Instant storeNow();
}
public final class systems.zlink.framework.locations.ZLinkAuthorityDeleted extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityWriteResult {
  public systems.zlink.framework.locations.ZLinkAuthorityDeleted(java.lang.String, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String storeVersion();
  public java.time.Instant storeNow();
}
public final class systems.zlink.framework.locations.ZLinkAuthorityConflict extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityWriteResult {
  public systems.zlink.framework.locations.ZLinkAuthorityConflict(systems.zlink.framework.locations.ZLinkAuthorityReadResult);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkAuthorityReadResult current();
}
public final class systems.zlink.framework.locations.ZLinkAuthorityGenerationExhausted extends java.lang.Record implements systems.zlink.framework.locations.ZLinkAuthorityWriteResult {
  public systems.zlink.framework.locations.ZLinkAuthorityGenerationExhausted();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public interface systems.zlink.framework.locations.ZLinkAuthorityStore {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityReadResult> read(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityWriteResult> compareExchange(java.lang.String, systems.zlink.framework.locations.ZLinkAuthorityExpectation, systems.zlink.framework.locations.ZLinkAuthorityMutation, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityScanResult> list(java.lang.String, java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor>, int, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectReserveResult> reserve(systems.zlink.framework.locations.ZLinkObjectReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectCommitResult> commit(systems.zlink.framework.locations.ZLinkObjectReservation, byte[], systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectCreationCompleteResult> completeCreation(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkObjectCreationCompletion, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkCreationTerminalReadResult> readCreationTerminal(systems.zlink.framework.locations.ZLinkCreationOperationIdentity, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkObjectAbortResult> abort(systems.zlink.framework.locations.ZLinkObjectReservation, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityReserveResult> reserveRelocationCapacity(systems.zlink.framework.locations.ZLinkRelocationCapacityReservationRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationCapacityAbortResult> abortRelocationCapacity(systems.zlink.framework.locations.ZLinkRelocationCapacityFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregatePrepareResult> prepareAggregate(systems.zlink.framework.locations.ZLinkAggregatePrepareRequest, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregateCommitResult> commitAggregate(systems.zlink.framework.locations.ZLinkAggregateFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAggregateAbortResult> abortAggregate(systems.zlink.framework.locations.ZLinkAggregateFence, systems.zlink.framework.locations.ZLinkStoreCancellation);
}
public final class systems.zlink.framework.locations.ZLinkRelocationStored extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkRelocationStored(java.lang.String, long, java.time.Instant, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String reference();
  public long checksumCrc32c();
  public java.time.Instant expiresAt();
  public java.time.Instant storeNow();
}
public sealed interface systems.zlink.framework.locations.ZLinkRelocationReadResult
    permits systems.zlink.framework.locations.ZLinkRelocationFound,
            systems.zlink.framework.locations.ZLinkRelocationMissing {
}
public final class systems.zlink.framework.locations.ZLinkRelocationFound extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationReadResult {
  public systems.zlink.framework.locations.ZLinkRelocationFound(byte[]);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public byte[] payload();
}
public final class systems.zlink.framework.locations.ZLinkRelocationMissing extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationReadResult {
  public systems.zlink.framework.locations.ZLinkRelocationMissing();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.locations.ZLinkRelocationDeleteResult extends java.lang.Enum<systems.zlink.framework.locations.ZLinkRelocationDeleteResult> {
  public static final systems.zlink.framework.locations.ZLinkRelocationDeleteResult DELETED;
  public static final systems.zlink.framework.locations.ZLinkRelocationDeleteResult MISSING;
  public static systems.zlink.framework.locations.ZLinkRelocationDeleteResult[] values();
  public static systems.zlink.framework.locations.ZLinkRelocationDeleteResult valueOf(java.lang.String);
}
public sealed interface systems.zlink.framework.locations.ZLinkRelocationRenewResult permits systems.zlink.framework.locations.ZLinkRelocationRenewed, systems.zlink.framework.locations.ZLinkRelocationRenewMissing {
}
public final class systems.zlink.framework.locations.ZLinkRelocationRenewed extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationRenewResult {
  public systems.zlink.framework.locations.ZLinkRelocationRenewed(java.time.Instant, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.time.Instant expiresAt();
  public java.time.Instant storeNow();
}
public final class systems.zlink.framework.locations.ZLinkRelocationRenewMissing extends java.lang.Record implements systems.zlink.framework.locations.ZLinkRelocationRenewResult {
  public systems.zlink.framework.locations.ZLinkRelocationRenewMissing();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public interface systems.zlink.framework.locations.ZLinkRelocationStore {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationStored> put(byte[], java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationReadResult> get(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationRenewResult> renew(java.lang.String, java.time.Duration, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRelocationDeleteResult> delete(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
}
public interface systems.zlink.framework.locations.ZLinkLocationChangeStampStore {
  public abstract java.util.concurrent.CompletionStage<java.lang.Long> getChangeStamp(systems.zlink.framework.locations.ZLinkLocationChangeStampScope);
}
public final class systems.zlink.framework.locations.ZLinkLocationOwnerToken extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationOwnerToken(java.lang.String, long);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String ownerId();
  public long leaseGeneration();
}
public interface systems.zlink.framework.locations.ZLinkLocationReadiness {
  public abstract java.util.concurrent.CompletionStage<java.lang.Boolean> isPeerReady(java.lang.String, systems.zlink.framework.locations.ZLinkLocationRole, systems.zlink.contracts.core.RoutingId);
}
public interface systems.zlink.framework.locations.ZLinkLocationRuntimeQuery {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationRuntimeStatus> getStatus();
  public abstract java.util.concurrent.CompletionStage<java.util.List<systems.zlink.framework.locations.ZLinkPeerLocation>> listPeerLocations(systems.zlink.framework.locations.ZLinkPeerLocationFilter);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkSpotLocation>> listSpotLocations(systems.zlink.framework.locations.ZLinkSpotLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkActorLocation>> listActorLocations(systems.zlink.framework.locations.ZLinkActorLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkRouteLocation>> listRouteLocations(systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkLocationTopologyEntry>> listTopology(systems.zlink.framework.locations.ZLinkLocationTopologyFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public abstract java.util.concurrent.CompletionStage<java.util.List<systems.zlink.framework.locations.ZLinkLocationServiceSummary>> listServiceSummaries(systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter);
}
public interface systems.zlink.framework.locations.ZLinkLocationWatchStore {
  public abstract java.util.concurrent.Flow$Publisher<systems.zlink.framework.locations.ZLinkLocationChanged> watch(systems.zlink.framework.locations.ZLinkLocationWatchFilter);
}
public final class systems.zlink.framework.locations.ZLinkLocationWriteIntent extends java.lang.Enum<systems.zlink.framework.locations.ZLinkLocationWriteIntent> {
  public static final systems.zlink.framework.locations.ZLinkLocationWriteIntent NEW_CLAIM;
  public static final systems.zlink.framework.locations.ZLinkLocationWriteIntent RENEW;
  public static final systems.zlink.framework.locations.ZLinkLocationWriteIntent TAKEOVER;
  public static systems.zlink.framework.locations.ZLinkLocationWriteIntent[] values();
  public static systems.zlink.framework.locations.ZLinkLocationWriteIntent valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkLocationWriteResult extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationWriteResult(systems.zlink.framework.locations.ZLinkLocationWriteStatus, long, java.time.Instant);
  public static systems.zlink.framework.locations.ZLinkLocationWriteResult stored(long, java.time.Instant);
  public static systems.zlink.framework.locations.ZLinkLocationWriteResult ignoredStale();
  public static systems.zlink.framework.locations.ZLinkLocationWriteResult rejectedConflict();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationWriteStatus status();
  public long generation();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkLocationWriteStatus extends java.lang.Enum<systems.zlink.framework.locations.ZLinkLocationWriteStatus> {
  public static final systems.zlink.framework.locations.ZLinkLocationWriteStatus STORED;
  public static final systems.zlink.framework.locations.ZLinkLocationWriteStatus IGNORED_STALE;
  public static final systems.zlink.framework.locations.ZLinkLocationWriteStatus REJECTED_CONFLICT;
  public static systems.zlink.framework.locations.ZLinkLocationWriteStatus[] values();
  public static systems.zlink.framework.locations.ZLinkLocationWriteStatus valueOf(java.lang.String);
  public int value();
}
public sealed interface systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult
    permits systems.zlink.framework.locations.ZLinkOwnerLeaseClaimed,
            systems.zlink.framework.locations.ZLinkOwnerLeaseClaimConflict,
            systems.zlink.framework.locations.ZLinkOwnerLeaseGenerationExhausted {
}
public final class systems.zlink.framework.locations.ZLinkOwnerLeaseClaimed extends java.lang.Record implements systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult {
  public systems.zlink.framework.locations.ZLinkOwnerLeaseClaimed(systems.zlink.framework.locations.ZLinkLocationOwnerToken, java.time.Instant, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationOwnerToken token();
  public java.time.Instant leaseExpiresAt();
  public java.time.Instant storeNow();
}
public final class systems.zlink.framework.locations.ZLinkOwnerLeaseClaimConflict extends java.lang.Record implements systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult {
  public systems.zlink.framework.locations.ZLinkOwnerLeaseClaimConflict();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.locations.ZLinkOwnerLeaseGenerationExhausted extends java.lang.Record implements systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult {
  public systems.zlink.framework.locations.ZLinkOwnerLeaseGenerationExhausted();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public sealed interface systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult
    permits systems.zlink.framework.locations.ZLinkOwnerLeaseRenewed,
            systems.zlink.framework.locations.ZLinkOwnerLeaseRenewStale {
}
public final class systems.zlink.framework.locations.ZLinkOwnerLeaseRenewed extends java.lang.Record implements systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult {
  public systems.zlink.framework.locations.ZLinkOwnerLeaseRenewed(java.time.Instant, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.time.Instant leaseExpiresAt();
  public java.time.Instant storeNow();
}
public final class systems.zlink.framework.locations.ZLinkOwnerLeaseRenewStale extends java.lang.Record implements systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult {
  public systems.zlink.framework.locations.ZLinkOwnerLeaseRenewStale();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public final class systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult extends java.lang.Enum<systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult> {
  public static final systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult RELEASED;
  public static final systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult STALE;
  public static systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult[] values();
  public static systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult valueOf(java.lang.String);
}
public sealed interface systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult
    permits systems.zlink.framework.locations.ZLinkOwnerLeaseFound,
            systems.zlink.framework.locations.ZLinkOwnerLeaseMissing {
}
public final class systems.zlink.framework.locations.ZLinkOwnerLeaseFound extends java.lang.Record implements systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult {
  public systems.zlink.framework.locations.ZLinkOwnerLeaseFound(systems.zlink.framework.locations.ZLinkLocationOwnerToken, java.time.Instant, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationOwnerToken token();
  public java.time.Instant leaseExpiresAt();
  public java.time.Instant storeNow();
}
public final class systems.zlink.framework.locations.ZLinkOwnerLeaseMissing extends java.lang.Record implements systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult {
  public systems.zlink.framework.locations.ZLinkOwnerLeaseMissing();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
}
public interface systems.zlink.framework.locations.ZLinkOwnerLeaseStore {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult> claimOwnerLease(java.lang.String, java.time.Duration);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult> readOwnerLease(java.lang.String);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult> renewOwnerLease(systems.zlink.framework.locations.ZLinkLocationOwnerToken, java.time.Duration);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult> releaseOwnerLease(systems.zlink.framework.locations.ZLinkLocationOwnerToken);
}
```

### General location compatibility surface

이 generic location capability는 application-defined discovery를 위해 유지한다. MeshNode, ClientServer와 fanout service runtime은 이를 재사용하지 않고 각 전용 store를 사용한다.

```java
public final class systems.zlink.framework.locations.ZLinkPeerLocation extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkPeerLocation(systems.zlink.framework.locations.ZLinkLocationAutoConnectType, java.lang.String, systems.zlink.contracts.core.RoutingId, systems.zlink.framework.locations.ZLinkLocationRole, java.lang.String, long, boolean, long, java.util.Map<java.lang.String, java.lang.String>, java.util.List<java.lang.String>, java.lang.String, long, long, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationAutoConnectType autoConnectType();
  public java.lang.String meshName();
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public systems.zlink.framework.locations.ZLinkLocationRole role();
  public java.lang.String endpoint();
  public long weight();
  public boolean draining();
  public long value();
  public java.util.Map<java.lang.String, java.lang.String> metadata();
  public java.util.List<java.lang.String> capabilities();
  public java.lang.String ownerId();
  public long leaseGeneration();
  public long generation();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkPeerLocationKey extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkPeerLocationKey(systems.zlink.framework.locations.ZLinkLocationAutoConnectType, java.lang.String, systems.zlink.framework.locations.ZLinkLocationRole, systems.zlink.contracts.core.RoutingId, java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationAutoConnectType autoConnectType();
  public java.lang.String meshName();
  public systems.zlink.framework.locations.ZLinkLocationRole role();
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public java.lang.String endpoint();
}
public final class systems.zlink.framework.locations.ZLinkPeerLocationFilter extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkPeerLocationFilter(systems.zlink.framework.locations.ZLinkLocationAutoConnectType, java.lang.String, systems.zlink.framework.locations.ZLinkLocationRole, systems.zlink.contracts.core.RoutingId, java.lang.String);
  public static systems.zlink.framework.locations.ZLinkPeerLocationFilter all();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationAutoConnectType autoConnectType();
  public java.lang.String meshName();
  public systems.zlink.framework.locations.ZLinkLocationRole role();
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public java.lang.String endpoint();
}
public interface systems.zlink.framework.locations.ZLinkPeerLocationStore {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updatePeer(systems.zlink.framework.locations.ZLinkPeerLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> removePeer(systems.zlink.framework.locations.ZLinkPeerLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkPeerLocation>> listPeerLocations(systems.zlink.framework.locations.ZLinkPeerLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
}
public interface systems.zlink.framework.locations.ZLinkPeerLocationResolver {
  public abstract java.util.concurrent.CompletionStage<java.util.List<systems.zlink.framework.locations.ZLinkPeerLocation>> listLivePeers(systems.zlink.framework.locations.ZLinkPeerLocationFilter);
}
public final class systems.zlink.framework.locations.ZLinkRouteLocation extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkRouteLocation(systems.zlink.framework.locations.ZLinkRouteKind, java.lang.String, systems.zlink.contracts.core.RoutingId, java.lang.String, long, long, byte[], java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkRouteKind routeKind();
  public java.lang.String routeKey();
  public systems.zlink.contracts.core.RoutingId ownerNodeRid();
  public java.lang.String ownerId();
  public long leaseGeneration();
  public long generation();
  public byte[] value();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkRouteLocationKey extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkRouteLocationKey(systems.zlink.framework.locations.ZLinkRouteKind, java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkRouteKind routeKind();
  public java.lang.String routeKey();
}
public final class systems.zlink.framework.locations.ZLinkRouteLocationFilter extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkRouteLocationFilter(systems.zlink.framework.locations.ZLinkRouteKind, systems.zlink.contracts.core.RoutingId, java.lang.String);
  public static systems.zlink.framework.locations.ZLinkRouteLocationFilter all();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkRouteKind routeKind();
  public systems.zlink.contracts.core.RoutingId ownerNodeRid();
  public java.lang.String ownerId();
}
public interface systems.zlink.framework.locations.ZLinkRouteLocationStore {
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateRoute(systems.zlink.framework.locations.ZLinkRouteLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> removeRoute(systems.zlink.framework.locations.ZLinkRouteLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRouteLocation> resolveRoute(systems.zlink.framework.locations.ZLinkRouteLocationKey);
  public abstract java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkRouteLocation>> listRouteLocations(systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
}
public interface systems.zlink.framework.locations.ZLinkLocationKey {
  public abstract systems.zlink.framework.locations.ZLinkLocationKind kind();
}
```

## Location query와 provider public signature

```java
public final class systems.zlink.framework.locations.ZLinkActorLocation extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkActorLocation(java.lang.String, java.lang.String, java.lang.String, systems.zlink.framework.actors.ActorRef, systems.zlink.contracts.core.RoutingId, long, java.lang.String, long, systems.zlink.framework.spots.ZLinkSpotKind, java.lang.String, long, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String meshName();
  public java.lang.String actorId();
  public java.lang.String actorType();
  public systems.zlink.framework.actors.ActorRef actorRef();
  public systems.zlink.contracts.core.RoutingId ownerNodeRid();
  public long ownerNodeGeneration();
  public java.lang.String spotId();
  public long spotGeneration();
  public systems.zlink.framework.spots.ZLinkSpotKind spotKind();
  public java.lang.String ownerId();
  public long leaseGeneration();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkActorLocationFilter extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkActorLocationFilter(java.lang.String, systems.zlink.contracts.core.RoutingId, java.lang.String, systems.zlink.framework.spots.ZLinkSpotKind);
  public static systems.zlink.framework.locations.ZLinkActorLocationFilter all();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String actorType();
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public java.lang.String spotId();
  public systems.zlink.framework.spots.ZLinkSpotKind locationKind();
}
public final class systems.zlink.framework.locations.ZLinkActorLocationKey extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkActorLocationKey(java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String actorId();
}
public final class systems.zlink.framework.locations.ZLinkLocationAutoConnectType extends java.lang.Enum<systems.zlink.framework.locations.ZLinkLocationAutoConnectType> {
  public static final systems.zlink.framework.locations.ZLinkLocationAutoConnectType INVALID;
  public static final systems.zlink.framework.locations.ZLinkLocationAutoConnectType ROUTE_MESH;
  public static final systems.zlink.framework.locations.ZLinkLocationAutoConnectType CLIENT_SERVER;
  public static final systems.zlink.framework.locations.ZLinkLocationAutoConnectType DEALER_MESH;
  public static final systems.zlink.framework.locations.ZLinkLocationAutoConnectType FANOUT;
  public static final systems.zlink.framework.locations.ZLinkLocationAutoConnectType SPOT_MESH;
  public static systems.zlink.framework.locations.ZLinkLocationAutoConnectType[] values();
  public static systems.zlink.framework.locations.ZLinkLocationAutoConnectType valueOf(java.lang.String);
  public int value();
  public static systems.zlink.framework.locations.ZLinkLocationAutoConnectType fromValue(int);
}
public final class systems.zlink.framework.locations.ZLinkLocationChangeStampScope extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationChangeStampScope(systems.zlink.framework.locations.ZLinkLocationKind, java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public java.lang.String meshName();
}
public final class systems.zlink.framework.locations.ZLinkLocationChangeType extends java.lang.Enum<systems.zlink.framework.locations.ZLinkLocationChangeType> {
  public static final systems.zlink.framework.locations.ZLinkLocationChangeType UPSERTED;
  public static final systems.zlink.framework.locations.ZLinkLocationChangeType REMOVED;
  public static final systems.zlink.framework.locations.ZLinkLocationChangeType EXPIRED;
  public static systems.zlink.framework.locations.ZLinkLocationChangeType[] values();
  public static systems.zlink.framework.locations.ZLinkLocationChangeType valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkLocationChanged extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationChanged(systems.zlink.framework.locations.ZLinkLocationKind, systems.zlink.framework.locations.ZLinkLocationKey, systems.zlink.framework.locations.ZLinkLocationChangeType, long, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public systems.zlink.framework.locations.ZLinkLocationKey key();
  public systems.zlink.framework.locations.ZLinkLocationChangeType changeType();
  public long generation();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkLocationKey$Actor extends java.lang.Record implements systems.zlink.framework.locations.ZLinkLocationKey {
  public systems.zlink.framework.locations.ZLinkLocationKey$Actor(systems.zlink.framework.locations.ZLinkActorLocationKey);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkActorLocationKey key();
}
public final class systems.zlink.framework.locations.ZLinkLocationKey$Peer extends java.lang.Record implements systems.zlink.framework.locations.ZLinkLocationKey {
  public systems.zlink.framework.locations.ZLinkLocationKey$Peer(systems.zlink.framework.locations.ZLinkPeerLocationKey);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkPeerLocationKey key();
}
public final class systems.zlink.framework.locations.ZLinkLocationKey$Route extends java.lang.Record implements systems.zlink.framework.locations.ZLinkLocationKey {
  public systems.zlink.framework.locations.ZLinkLocationKey$Route(systems.zlink.framework.locations.ZLinkRouteLocationKey);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkRouteLocationKey key();
}
public final class systems.zlink.framework.locations.ZLinkLocationKey$Spot extends java.lang.Record implements systems.zlink.framework.locations.ZLinkLocationKey {
  public systems.zlink.framework.locations.ZLinkLocationKey$Spot(systems.zlink.framework.locations.ZLinkSpotLocationKey);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkSpotLocationKey key();
}
public final class systems.zlink.framework.locations.ZLinkLocationKind extends java.lang.Enum<systems.zlink.framework.locations.ZLinkLocationKind> {
  public static final systems.zlink.framework.locations.ZLinkLocationKind INVALID;
  public static final systems.zlink.framework.locations.ZLinkLocationKind PEER;
  public static final systems.zlink.framework.locations.ZLinkLocationKind SPOT;
  public static final systems.zlink.framework.locations.ZLinkLocationKind ACTOR;
  public static final systems.zlink.framework.locations.ZLinkLocationKind ROUTE;
  public static systems.zlink.framework.locations.ZLinkLocationKind[] values();
  public static systems.zlink.framework.locations.ZLinkLocationKind valueOf(java.lang.String);
  public int value();
  public static systems.zlink.framework.locations.ZLinkLocationKind fromValue(int);
}
public final class systems.zlink.framework.locations.ZLinkLocationPage<T> extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationPage(java.util.List<T>, java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.util.List<T> items();
  public java.lang.String continuationToken();
}
public final class systems.zlink.framework.locations.ZLinkLocationRole extends java.lang.Enum<systems.zlink.framework.locations.ZLinkLocationRole> {
  public static final systems.zlink.framework.locations.ZLinkLocationRole INVALID;
  public static final systems.zlink.framework.locations.ZLinkLocationRole SPOT;
  public static final systems.zlink.framework.locations.ZLinkLocationRole ROUTER;
  public static final systems.zlink.framework.locations.ZLinkLocationRole DEALER;
  public static final systems.zlink.framework.locations.ZLinkLocationRole PUB;
  public static final systems.zlink.framework.locations.ZLinkLocationRole SUB;
  public static systems.zlink.framework.locations.ZLinkLocationRole[] values();
  public static systems.zlink.framework.locations.ZLinkLocationRole valueOf(java.lang.String);
  public int value();
  public static systems.zlink.framework.locations.ZLinkLocationRole fromValue(int);
}
public final class systems.zlink.framework.locations.ZLinkLocationRuntimeStatus extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationRuntimeStatus(boolean, boolean, java.time.Duration, java.time.Instant, java.lang.String, boolean, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public boolean storeHealthy();
  public boolean watchEnabled();
  public java.time.Duration pollingInterval();
  public java.time.Instant lastRefreshAt();
  public java.lang.String lastError();
  public boolean ownerLeaseHealthy();
  public java.time.Instant ownerLeaseRenewedAt();
}
public final class systems.zlink.framework.locations.ZLinkLocationServiceSummary extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationServiceSummary(java.lang.String, systems.zlink.framework.locations.ZLinkLocationAutoConnectType, systems.zlink.framework.locations.ZLinkLocationRole, long, long, long, long, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String meshName();
  public systems.zlink.framework.locations.ZLinkLocationAutoConnectType autoConnectType();
  public systems.zlink.framework.locations.ZLinkLocationRole role();
  public long totalCount();
  public long readyCount();
  public long errorCount();
  public long stoppedCount();
  public java.time.Instant lastUpdatedAt();
}
public final class systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter(java.lang.String, systems.zlink.framework.locations.ZLinkLocationAutoConnectType, systems.zlink.framework.locations.ZLinkLocationRole);
  public static systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter all();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String meshName();
  public systems.zlink.framework.locations.ZLinkLocationAutoConnectType autoConnectType();
  public systems.zlink.framework.locations.ZLinkLocationRole role();
}
public interface systems.zlink.framework.locations.ZLinkLocationStore extends systems.zlink.framework.locations.ZLinkMeshNodeLocationStore,systems.zlink.framework.locations.ZLinkOwnerLeaseStore,systems.zlink.framework.locations.ZLinkAuthorityStore {
  public abstract java.util.concurrent.CompletionStage<java.lang.Long> removeAllByOwner(systems.zlink.framework.locations.ZLinkLocationOwnerToken);
}
public final class systems.zlink.framework.locations.ZLinkLocationTopologyEntry extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationTopologyEntry(systems.zlink.framework.locations.ZLinkLocationKind, java.lang.String, systems.zlink.framework.locations.ZLinkLocationRole, systems.zlink.contracts.core.RoutingId, systems.zlink.contracts.core.RoutingId, java.lang.String, java.lang.String, systems.zlink.framework.locations.ZLinkLocationTopologyState, long, long, int, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public java.lang.String meshName();
  public systems.zlink.framework.locations.ZLinkLocationRole role();
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public java.lang.String spotId();
  public java.lang.String actorId();
  public java.lang.String endpoint();
  public systems.zlink.framework.locations.ZLinkLocationTopologyState state();
  public long desiredCount();
  public long readyCount();
  public int errorCode();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkLocationTopologyFilter extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationTopologyFilter(systems.zlink.framework.locations.ZLinkLocationKind, java.lang.String, systems.zlink.framework.locations.ZLinkLocationRole, systems.zlink.contracts.core.RoutingId, systems.zlink.framework.locations.ZLinkLocationTopologyState);
  public static systems.zlink.framework.locations.ZLinkLocationTopologyFilter all();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public java.lang.String meshName();
  public systems.zlink.framework.locations.ZLinkLocationRole role();
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public systems.zlink.framework.locations.ZLinkLocationTopologyState state();
}
public final class systems.zlink.framework.locations.ZLinkLocationTopologyState extends java.lang.Enum<systems.zlink.framework.locations.ZLinkLocationTopologyState> {
  public static final systems.zlink.framework.locations.ZLinkLocationTopologyState DISCOVERED;
  public static final systems.zlink.framework.locations.ZLinkLocationTopologyState CONNECTING;
  public static final systems.zlink.framework.locations.ZLinkLocationTopologyState READY;
  public static final systems.zlink.framework.locations.ZLinkLocationTopologyState LOST;
  public static final systems.zlink.framework.locations.ZLinkLocationTopologyState ERROR;
  public static final systems.zlink.framework.locations.ZLinkLocationTopologyState STOPPED;
  public static systems.zlink.framework.locations.ZLinkLocationTopologyState[] values();
  public static systems.zlink.framework.locations.ZLinkLocationTopologyState valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.locations.ZLinkLocationWatchFilter extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkLocationWatchFilter(systems.zlink.framework.locations.ZLinkLocationKind, java.lang.String, systems.zlink.framework.locations.ZLinkRouteKind);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.framework.locations.ZLinkLocationKind kind();
  public java.lang.String meshName();
  public systems.zlink.framework.locations.ZLinkRouteKind routeKind();
}
public final class systems.zlink.framework.locations.ZLinkPageRequest extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkPageRequest(int, java.lang.String);
  public static systems.zlink.framework.locations.ZLinkPageRequest firstPage();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public int pageSize();
  public java.lang.String continuationToken();
}
public final class systems.zlink.framework.locations.ZLinkRouteKind extends java.lang.Enum<systems.zlink.framework.locations.ZLinkRouteKind> {
  public static final systems.zlink.framework.locations.ZLinkRouteKind INVALID;
  public static final systems.zlink.framework.locations.ZLinkRouteKind ACTOR_SESSION;
  public static final systems.zlink.framework.locations.ZLinkRouteKind SPOT_NAME;
  public static final systems.zlink.framework.locations.ZLinkRouteKind FRAMEWORK_ROUTE;
  public static systems.zlink.framework.locations.ZLinkRouteKind[] values();
  public static systems.zlink.framework.locations.ZLinkRouteKind valueOf(java.lang.String);
  public int value();
  public static systems.zlink.framework.locations.ZLinkRouteKind fromValue(int);
}
public final class systems.zlink.framework.locations.ZLinkSpotLocation extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkSpotLocation(java.lang.String, java.lang.String, long, systems.zlink.contracts.core.RoutingId, long, systems.zlink.framework.spots.ZLinkSpotKind, java.lang.String, java.lang.String, long, java.time.Instant);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String meshName();
  public java.lang.String spotId();
  public long spotGeneration();
  public systems.zlink.contracts.core.RoutingId ownerNodeRid();
  public long ownerNodeGeneration();
  public systems.zlink.framework.spots.ZLinkSpotKind spotKind();
  public java.lang.String spotType();
  public java.lang.String ownerId();
  public long leaseGeneration();
  public java.time.Instant updatedAt();
}
public final class systems.zlink.framework.locations.ZLinkSpotLocationFilter extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkSpotLocationFilter(java.lang.String, java.lang.String, systems.zlink.contracts.core.RoutingId, systems.zlink.framework.spots.ZLinkSpotKind);
  public static systems.zlink.framework.locations.ZLinkSpotLocationFilter all();
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String meshName();
  public java.lang.String spotType();
  public systems.zlink.contracts.core.RoutingId nodeRid();
  public systems.zlink.framework.spots.ZLinkSpotKind spotKind();
}
public final class systems.zlink.framework.locations.ZLinkSpotLocationKey extends java.lang.Record {
  public systems.zlink.framework.locations.ZLinkSpotLocationKey(java.lang.String);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String spotId();
}
```
