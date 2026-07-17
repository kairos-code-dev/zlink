# Java Location Store·Redis 공개 인터페이스

[Java 계약 목차](README.ko.md) · [Location Runtime](../../40-location-runtime.ko.md) ·
[Redis Location Store](../../41-location-store-redis.ko.md)

## 1. Root 등록과 option

```java
public final class ZLinkLocationOptions {
    Duration heartbeatInterval();
    void setHeartbeatInterval(Duration value);
    Duration ownerLeaseTtl();
    void setOwnerLeaseTtl(Duration value);
    Duration pollingInterval();
    void setPollingInterval(Duration value);
    Duration storeFailureGrace();
    void setStoreFailureGrace(Duration value);
    Duration routingIdFencingMargin();
    void setRoutingIdFencingMargin(Duration value);
    Duration ownerLeaseRenewTimeout();
    void setOwnerLeaseRenewTimeout(Duration value);
}
```

Root의 `addLocationStore(...)`와 `configureLocations()`는
[Java interface catalog §4](02-handler-interfaces.ko.md#4-client-와-options)가 한 번만 선언한다. 이 문서는
두 메서드가 사용하는 option과 store capability 타입을 소유한다.

각 option의 기본값은 다음과 같다. 여섯 값은 모두 0보다 커야 한다.

| option | 기본값 |
|---|---:|
| `heartbeatInterval` | 10초 |
| `ownerLeaseTtl` | 30초 |
| `pollingInterval` | 1초 |
| `storeFailureGrace` | 30초 |
| `routingIdFencingMargin` | 5초 |
| `ownerLeaseRenewTimeout` | 3초 |

Routing ID 자동 할당을 사용하면 다음 관계도 만족해야 한다. 만족하지 않으면 Spring host는 socket bind 전에
설정 오류로 종료한다. 이 검증은
[Location Runtime §2](../../40-location-runtime.ko.md#2-record-분리)의 공통 계약을
Java `Duration`으로 투영한 것이다.

```text
heartbeatInterval + ownerLeaseRenewTimeout
    < ownerLeaseTtl - routingIdFencingMargin
```

Store는 application context마다 하나만 등록한다. 자동 discovery, remote Spot·Actor 위치, routing ID 자동
할당 또는 Actor transfer를 설정했는데 store나 필요한 capability가 없으면 Spring host는 socket bind 전에
startup validation 오류로 종료한다.

## 2. Store-neutral record와 capability

이 절의 `generation`, `revision`, `epoch`과 transfer 정수는 공통 Redis 계약의 unsigned 64-bit
값을 Java `long`의 64-bit bit pattern으로 표현한다. Redis 10진 문자열은
`Long.toUnsignedString(...)`과 unsigned parse로 변환하고, 순서 비교는 `Long.compareUnsigned(...)`를
사용한다. Signed 비교나 음수 여부로 generation의 순서와 stale 상태를 판정하지 않는다.

```java
public enum ZLinkLocationWriteIntent { NEW_CLAIM, RENEW, TAKEOVER }
public enum ZLinkLocationWriteStatus { STORED, IGNORED_STALE, REJECTED_CONFLICT }

public record ZLinkLocationWriteResult(
    ZLinkLocationWriteStatus status, long generation, Instant updatedAt) {}
public record ZLinkLocationOwnerToken(String ownerId, long generation) {}
public record ZLinkOwnerLease(
    String ownerId, RoutingId nodeRid, Instant leaseExpiresAt, Instant updatedAt) {}
public record ZLinkOwnerLeaseRenewal(Instant leaseExpiresAt, Instant storeNow) {}
public record ZLinkOwnerLeaseSnapshot(List<ZLinkOwnerLease> leases, Instant storeNow) {}

public record ZLinkMeshNodeDescriptor(
    String meshName,
    RoutingId rid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    Map<String, Integer> channelWeights,
    boolean draining,
    String securityIdentity,
    String ownerId,
    Instant updatedAt) {}
public record ZLinkMeshNodeDescriptorKey(String meshName, RoutingId rid) {}

public record ZLinkSpotLocation(
    String meshName, RoutingId spotRid, long spotGeneration,
    RoutingId ownerNodeRid, long ownerNodeGeneration, ZLinkSpotKind spotKind,
    String spotType, String ownerId, Instant updatedAt) {}
public record ZLinkSpotLocationKey(String meshName, RoutingId spotRid) {}

public record ZLinkActorLocation(
    String meshName, String actorId, String actorType, ActorRef actorRef,
    RoutingId ownerNodeRid, long ownerNodeGeneration, RoutingId spotRid,
    long spotGeneration, ZLinkSpotKind spotKind, long membershipEpoch,
    String ownerId, Instant updatedAt) {}
public record ZLinkActorLocationKey(String meshName, String actorId) {}

public interface ZLinkMeshNodeLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateMeshNode(
        ZLinkMeshNodeDescriptor descriptor, ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeMeshNode(
        ZLinkMeshNodeDescriptorKey key, ZLinkLocationOwnerToken owner);
    CompletionStage<List<ZLinkMeshNodeDescriptor>> listMeshNodes(String meshName);
}

public interface ZLinkSpotLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateSpot(
        ZLinkSpotLocation location, ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeSpot(
        ZLinkSpotLocationKey key, ZLinkLocationOwnerToken owner);
    CompletionStage<Optional<ZLinkSpotLocation>> resolveSpot(ZLinkSpotLocationKey key);
}

public interface ZLinkActorLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateActor(
        ZLinkActorLocation location, ZLinkLocationWriteIntent intent);
    CompletionStage<ZLinkLocationWriteStatus> removeActor(
        ZLinkActorLocationKey key, ZLinkLocationOwnerToken owner);
    CompletionStage<Optional<ZLinkActorLocation>> resolveActor(ZLinkActorLocationKey key);
}

public interface ZLinkOwnerLeaseStore {
    CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLease(
        String ownerId, RoutingId nodeRid, Duration leaseTtl);
    CompletionStage<Boolean> removeOwnerLease(String ownerId);
    CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeases();
}
```

## 3. Actor transfer authority

```java
public enum ZLinkActorTransferState { PREPARED, COMMITTED, ACTIVATED, ABORTED }
public enum ZLinkActorTransferWriteStatus {
    STORED, NOT_FOUND, IGNORED_STALE, REJECTED_CONFLICT, INVALID_STATE
}

public record ZLinkActorTransferRecord(
    String meshName, String actorId, UUID transferId, ActorRef source, ActorRef target,
    long expectedActorGeneration, long expectedMembershipEpoch,
    Set<RoutingId> participants, ZLinkActorTransferState state,
    String recoveryOwnerId, Instant recoveryLeaseExpiresAt, Instant updatedAt) {}

public record ZLinkActorTransferPrepareRequest(
    String meshName, String actorId, UUID transferId, ActorRef source, ActorRef target,
    long expectedActorGeneration, long expectedMembershipEpoch,
    Set<RoutingId> participants, String recoveryOwnerId, Duration recoveryLeaseTtl) {}

public record ZLinkActorTransferWriteResult(
    ZLinkActorTransferWriteStatus status, Optional<ZLinkActorTransferRecord> record) {}

public interface ZLinkActorTransferStore {
    CompletionStage<ZLinkActorTransferWriteResult> prepareActorTransfer(
        ZLinkActorTransferPrepareRequest request);
    CompletionStage<ZLinkActorTransferWriteResult> commitActorTransfer(
        String meshName, String actorId, UUID transferId, String recoveryOwnerId);
    CompletionStage<ZLinkActorTransferWriteResult> activateActorTransfer(
        String meshName, String actorId, UUID transferId, String recoveryOwnerId);
    CompletionStage<ZLinkActorTransferWriteResult> abortActorTransfer(
        String meshName, String actorId, UUID transferId, String recoveryOwnerId);
    CompletionStage<ZLinkActorTransferWriteResult> takeOverActorTransfer(
        String meshName, String actorId, UUID transferId,
        String successorOwnerId, Duration recoveryLeaseTtl);
    CompletionStage<Optional<ZLinkActorTransferRecord>> resolveActorTransfer(
        String meshName, String actorId);
}

public interface ZLinkLocationStore extends
    ZLinkMeshNodeLocationStore,
    ZLinkSpotLocationStore,
    ZLinkActorLocationStore,
    ZLinkOwnerLeaseStore,
    ZLinkActorTransferStore {
    CompletionStage<Long> removeAllByOwner(String ownerId);
}
```

`UUID transferId`는 Redis 경계에서 UUID 128-bit의 소문자 `8-4-4-4-12` 문자열로 변환한다. 읽을 때도 이
형식만 받아 같은 UUID 값으로 복원하며, 다른 문자열 표현은 store record로 허용하지 않는다.

Prepare는 active transfer 부재, Actor generation과 membership epoch를 한 원자 operation에서 비교한다.
Commit은 target owner와 정확히 다음 membership epoch를 함께 기록한다. Takeover는 recovery lease 만료,
participant set과 현재 Actor location을 같은 operation에서 확인한다.

## 4. 공식 Redis package

```java
package systems.zlink.framework.locations.redis;

public final class ZLinkRedisLocationOptions {
    public String connectionString();
    public ZLinkRedisLocationOptions connectionString(String value);
    public String keyPrefix();
    public ZLinkRedisLocationOptions keyPrefix(String value);
}

public final class ZLinkRedisLocationStore implements
    ZLinkLocationStore,
    ZLinkRoutingIdSlotAllocationStore,
    ZLinkLocationChangeStampStore,
    AutoCloseable {
    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options);
    public void close();
}
```

`connectionString`과 비어 있지 않은 `keyPrefix`는 필수다. Store가 Redis connection을 소유하며 `close()`가
시작된 뒤 새 operation은 `IllegalStateException`으로 실패한다.

## 5. 목표 계약 적용 추적

정식 계약은 위 시그니처다. Source와 package 적용이 남은 항목은 gap 문서가 추적하며 계약을 축소하지 않는다.

| gap | 적용 작업 |
|---|---|
| [IMP-JV-36 / §12.27](../../../gaps/java.ko.md) | `ZLinkActorLocation`과 Redis codec에 `spotGeneration`이 없다. |
| [IMP-JV-38 / §12.29](../../../gaps/java.ko.md) | `ZLinkActorTransferStore`와 공식 Redis prepare·commit·abort·takeover 구현이 없다. |
