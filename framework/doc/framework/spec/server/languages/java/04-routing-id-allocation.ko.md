# Java routing ID 자동 할당 공개 계약

[Java 계약 목차](README.ko.md) · [Java RouteMesh builder](02-handler-interfaces.ko.md#4-client-와-options) ·
[Location Store·Redis](03-location-store.ko.md) ·
[Redis slot 원자성](../../41-location-store-redis.ko.md#8-routing-id-slot-원자성)

## 1. 범위

이 문서는 MeshNode routing ID slot allocation의 정확한 Java store capability, 결과 타입과 준비 상태 조회
시그니처를 고정한다. MeshNode builder의 allocation 메서드는
[Java interface catalog](02-handler-interfaces.ko.md#4-client-와-options)가 소유하며 여기서 재선언하지
않는다.

한 allocation group은 같은 slot 번호를 공유하는 MeshNode member와 각 routing ID prefix를 묶는다. 같은
process가 서로 다른 MeshName의 MeshNode를 여러 개 등록하면 하나의 group에서 확정한 slot을 각 member의
routing ID에 적용할 수 있다.

## 2. Store capability

```java
public interface ZLinkRoutingIdSlotAllocationStore {
    CompletionStage<ZLinkRoutingIdSlotAcquireResult> acquireRoutingIdSlot(
        ZLinkRoutingIdSlotAcquireRequest request);

    CompletionStage<ZLinkRoutingIdSlotReleaseResult> releaseRoutingIdSlot(
        String groupName,
        int slot,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkRoutingIdSlotAllocationSnapshot> listRoutingIdSlots(
        String groupName);
}

public record ZLinkRoutingIdSlotAcquireRequest(
    String groupName,
    List<ZLinkRoutingIdSlotAllocationMember> members,
    int slotCount,
    String ownerId,
    Duration leaseTtl) {}

public record ZLinkRoutingIdSlotAllocationMember(
    String meshName,
    String routingIdPrefix) {}

public sealed interface ZLinkRoutingIdSlotAcquireResult
    permits ZLinkRoutingIdSlotAcquired,
            ZLinkRoutingIdSlotGroupExhausted,
            ZLinkRoutingIdSlotGroupConfigurationMismatch,
            ZLinkRoutingIdSlotIdentityModeConflict {
}

public record ZLinkRoutingIdSlotAcquired(
    ZLinkRoutingIdSlotAllocation allocation)
    implements ZLinkRoutingIdSlotAcquireResult {}

public record ZLinkRoutingIdSlotGroupExhausted()
    implements ZLinkRoutingIdSlotAcquireResult {}

public record ZLinkRoutingIdSlotGroupConfigurationMismatch(
    List<ZLinkRoutingIdSlotAllocationMember> expectedMembers,
    int expectedSlotCount,
    List<ZLinkRoutingIdSlotAllocationMember> actualMembers,
    int actualSlotCount)
    implements ZLinkRoutingIdSlotAcquireResult {}

public record ZLinkRoutingIdSlotIdentityModeConflict()
    implements ZLinkRoutingIdSlotAcquireResult {}

public record ZLinkRoutingIdSlotAllocation(
    int slot,
    ZLinkLocationOwnerToken owner,
    Instant leaseExpiresAt,
    Instant storeNow) {}

public enum ZLinkRoutingIdSlotReleaseResult {
    RELEASED,
    IGNORED_STALE
}

public record ZLinkRoutingIdSlotAllocationSnapshot(
    String groupName,
    List<ZLinkRoutingIdSlotAllocationMember> members,
    int slotCount,
    List<ZLinkRoutingIdSlotAllocation> allocations,
    Instant storeNow) {}
```

Acquire 결과는 네 record로 닫혀 있다. 같은 owner가 다시 호출하면 같은 slot과 generation을 반환한다.
서로 다른 owner가 같은 유효 slot을 동시에 받을 수 없다. Member 목록은 MeshName과 routing ID prefix를
기준으로 정규화하며, 같은 group의 첫 성공 이후에는 member 집합과 slot 수를 바꿀 수 없다.

Release는 group, slot, owner ID와 generation이 모두 일치할 때만 `RELEASED`다. 오래된 owner token은
`IGNORED_STALE`이며 현재 allocation을 바꾸지 않는다. 반환되는 `List`는 변경할 수 없는 snapshot이다.

## 3. Root store와 Redis capability

자동 할당은 별도 store 등록 함수를 제공하지 않는다. Root에 등록한 같은 `ZLinkLocationStore` instance가
`ZLinkRoutingIdSlotAllocationStore`도 구현해야 한다. 공식 `ZLinkRedisLocationStore`가 production
capability를 제공한다.

자동 할당이 설정되어 있는데 등록한 location store가 이 interface를 구현하지 않으면 Spring host는 socket
bind 전에 설정 오류로 종료한다. Fixed routing ID와 automatic allocation을 같은 MeshNode에 설정해도 같은
시점에 실패한다.

## 4. 준비된 결과 조회

```java
public interface ZLinkAllocatedRoutingIdProvider {
    CompletionStage<ZLinkAllocatedRoutingId> waitForReadyAllocation(
        String groupName);
}

public record ZLinkAllocatedRoutingId(
    String groupName,
    int slot,
    Map<String, RoutingId> meshNodeRoutingIds) {}
```

Map key는 MeshName이다. Provider는 group의 모든 MeshNode에 routing ID를 적용하고 socket bind, MeshNode
descriptor 게시와 readiness가 완료된 뒤 결과를 반환한다. 등록되지 않은 group은 설정 오류로 실패한다.
반환되는 Map은 변경할 수 없는 snapshot이다.

## 5. Startup 순서

1. 모든 allocation group과 lease option을 검증한다.
2. group 이름 순서로 slot과 owner lease를 확보한다.
3. 확정한 routing ID를 각 MeshNode에 적용한다.
4. MeshNode socket을 만들고 bind한다.
5. MeshNode descriptor를 게시한 뒤 readiness provider를 완료한다.

어느 group이 소진되면 확보한 다른 group의 slot을 release하고 bind를 시작하지 않는다. Lease renew를 안전
기한까지 확인하지 못하면 host는 관련 MeshNode의 종료를 요청하며, 실행 중에 새 slot을 임의로 선택하지
않는다.
