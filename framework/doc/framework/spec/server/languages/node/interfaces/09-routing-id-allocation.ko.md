# Node.js routing ID 자동 할당 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Node.js 계약 목차](../README.ko.md)

이 문서는 [Redis location store의 routing ID slot 계약](../../../41-location-store-redis.ko.md#7-routing-id-allocation)을
Node.js에서 표현하는 정확한 public interface를 고정한다.

## 1. builder

MeshNode와 classic fanout builder는 고정 routing ID 설정과 slot 기반 자동 할당 설정을
제공한다. Framework builder의 정확한 signature는
[기초 타입과 구성](01-foundation-configuration.ko.md)과
[Channel과 routing](02-channel-messaging.ko.md)이 소유하고, NestJS builder는
[NestJS host adapter](07-nestjs-host.ko.md)가 소유한다.

`routingId(...)`는 고정 identity를 설정한다. 첫 번째 자동 할당 overload는 등록 이름을 prefix로
사용한다. 두 번째 overload는 routing id prefix만 바꾸며
기본 group 이름은 유지한다. group 설정은 자동 할당 설정 전후 어느 쪽에도 둘 수 있다. 고정 routing
id와 자동 할당을 함께 설정하면 runtime 시작 전에 `ZLinkConfigurationException`으로 실패한다.

NestJS의 `ZLinkNestMeshNodeBuilder`와 `ZLinkNestFanoutChannelBuilder`도 같은 네 메서드를 제공한다. NestJS builder의
선택 설정 관례에 따라 고정 routing ID 인자는 `string | undefined`를 받는다.

## 2. allocation store capability

```ts
export interface ZLinkRoutingIdSlotAllocationStore {
  acquireRoutingIdSlot(
    request: ZLinkRoutingIdSlotAcquireRequest,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotAcquireResult>;

  releaseRoutingIdSlot(
    groupName: string,
    slot: number,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotReleaseResult>;

  listRoutingIdSlots(
    groupName: string,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotAllocationSnapshot>;
}
```

acquire 결과는 `kind`가 `acquired`, `groupExhausted`, `groupConfigurationMismatch`,
`identityModeConflict` 중 하나인 discriminated union이다. 성공 결과는 slot, 입력과 같은
owner token과 같은 원자 연산에서 읽은 store 시각을 포함한다. 별도 lease 만료 시각은 없다.
release 결과는 `released` 또는
`ignoredStale`다.

정확한 값 형태는 다음과 같다. member 목록은 `meshName`과 routing ID prefix 순서로
정규화한다.

```ts
export interface ZLinkRoutingIdSlotAllocationMember {
  readonly meshName: string;
  readonly routingIdPrefix: string;
}

export interface ZLinkRoutingIdSlotAcquireRequest {
  readonly groupName: string;
  readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
  readonly slotCount: number;
  readonly owner: ZLinkLocationOwnerToken;
}

export interface ZLinkRoutingIdSlotAllocation {
  readonly slot: number;
  readonly owner: ZLinkLocationOwnerToken;
  readonly storeNow: Date;
}

export type ZLinkRoutingIdSlotAcquireResult =
  | { readonly kind: 'acquired'; readonly allocation: ZLinkRoutingIdSlotAllocation }
  | { readonly kind: 'groupExhausted' }
  | {
      readonly kind: 'groupConfigurationMismatch';
      readonly expectedMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
      readonly expectedSlotCount: number;
      readonly actualMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
      readonly actualSlotCount: number;
    }
  | { readonly kind: 'identityModeConflict' };

export type ZLinkRoutingIdSlotReleaseResult = 'released' | 'ignoredStale';

export interface ZLinkRoutingIdSlotAllocationSnapshot {
  readonly groupName: string;
  readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
  readonly slotCount: number;
  readonly allocations: readonly ZLinkRoutingIdSlotAllocation[];
  readonly storeNow: Date;
}
```

Host는 lifecycle 시작에서 owner lease를 한 번 claim한 뒤 같은 token을 모든 slot acquire request에
넘긴다. Provider는 active host token을 slot 배정과 같은 원자 operation에서 확인한다. Slot은 별도
TTL이나 token을 발급하지 않고 성공 result에는 입력과 같은 token만 반환한다. Startup rollback은
확보한 slot을 먼저 release한 뒤 host lease를 마지막에 release한다.

`slotCount`와 allocation `slot`은 `1..65535`이고 `members.length`는 `1..255`다. 범위를 벗어난 builder
설정과 acquire request, 정수가 아닌 값은 startup 또는 provider validation에서 거부한다.
`listRoutingIdSlots`는 이 상한 안에서 group 전체를 한 coherent snapshot으로 반환하며 pagination하지 않는다.

## 3. 준비된 결과 조회

```ts
export interface ZLinkAllocatedRoutingIdProvider {
  waitForReadyAllocation(
    groupName: string,
    signal?: AbortSignal
  ): Promise<ZLinkAllocatedRoutingId>;
}

export const ZLINK_ALLOCATED_ROUTING_ID_PROVIDER: unique symbol;
```

결과는 group 이름, slot과 member 이름별 `RoutingId`를 제공한다. Provider는 모든 socket bind와 MeshNode
또는 fanout publisher 전용 descriptor 게시가 끝나 readiness에 도달한 뒤에만 완료된다. 등록되지 않은 group은
`ZLinkConfigurationException`으로 실패한다.

```ts
export interface ZLinkAllocatedRoutingId {
  readonly groupName: string;
  readonly slot: number;
  readonly memberRoutingIds: ReadonlyMap<string, RoutingId>;
}
```

MeshNode member의 `meshName`은 builder에 등록한 MeshName이다. `memberRoutingIds`는 이 이름을
key로 사용하므로 caller가 별도 object key 동등성을 관리할 필요가 없다.

Instance Spot은 allocation group의 member가 아니다. `InstanceSpotAddress.spotRid`는 application이 지정한
논리 RID이고, Framework가 MeshNode나 publisher RID slot에서 할당하지 않는다. MeshNode RID 할당과 listener
bind가 끝난 뒤 Framework는 실제 endpoint와 object kind가 `"instance_spot"`인 `objectCapabilities`를 같은
descriptor에 기록한다.

## 4. location option

`ZLinkLocationOptions`는 `ownerLeaseRenewIntervalMs`, `ownerLeaseTtlMs`와 함께
`ownerLeaseFencingMarginMs`, `ownerLeaseRenewTimeoutMs`를 제공한다. 기본값은 각각 5000, 15000,
5000, 3000이며 모든 Location owner lease host에 적용하는 공통 시간 관계를 만족해야 한다. Routing allocation은
같은 host lease token과 deadline을 소비하며 별도 margin 의미를 만들지 않는다.

## 5. Contract test

| 테스트 | 확인 기준 |
|--------|-----------|
| builder contract | 네 builder가 같은 allocation 설정을 등록하고 고정 routing id 충돌을 거부한다 |
| in-memory store contract | 최소 slot, 멱등 acquire, 만료 후 재사용과 stale release fencing을 확인한다 |
| Redis store contract | Lua 원자 할당, group 구성 고정과 stale release 차단을 확인한다 |
| runtime lifecycle | bind 전 할당, readiness 이후 조회, bind 실패와 종료의 release를 확인한다 |
