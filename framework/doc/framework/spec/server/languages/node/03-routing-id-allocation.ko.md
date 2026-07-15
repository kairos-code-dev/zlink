# Node.js routing id 자동 할당 공개 계약

이 문서는 [공통 location runtime 계약](../../40-location-runtime.ko.md#12-routing-id-slot-allocation)을
Node.js에서 표현하는 정확한 public interface를 고정한다.

## 1. builder

client/server channel, fanout channel, route mesh channel과 SpotNode builder는 다음 메서드를 제공한다.
반환형은 호출한 builder 자신이다.

```ts
routingId(routingId: string): this;
useAllocatedRoutingId(slotCount: number): this;
useAllocatedRoutingId(slotCount: number, routingIdPrefix: string): this;
setRoutingIdAllocationGroup(groupName: string): this;
```

`routingId(...)`는 기존 고정 identity 설정이다. 첫 번째 자동 할당 overload는 등록 이름을 prefix로
사용한다. 두 번째 overload는 routing id prefix만 바꾸며
기본 group 이름은 유지한다. group 설정은 자동 할당 설정 전후 어느 쪽에도 둘 수 있다. 고정 routing
id와 자동 할당을 함께 설정하면 runtime 시작 전에 `ZLinkConfigurationException`으로 실패한다.

NestJS의 `ZLinkNestClientServerChannelBuilder`, `ZLinkNestFanoutChannelBuilder`,
`ZLinkNestRouterMeshBuilder`, `ZLinkNestSpotNodeBuilder`도 같은 네 메서드를 제공한다. NestJS builder의
기존 선택 설정 관례에 따라 고정 routing id 인자는 `string | undefined`를 받는다.

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
`identityModeConflict` 중 하나인 discriminated union이다. 성공 결과는 slot, owner token, lease 만료
시각과 같은 원자 연산에서 읽은 store 시각을 포함한다. release 결과는 `released` 또는
`ignoredStale`다.

정확한 값 형태는 다음과 같다. member 목록은 `channelName` 오름차순으로 정규화한다. 이름은 channel과
SpotNode를 같은 저장소 계약으로 다루기 위해 `channelName`을 사용한다.

```ts
export interface ZLinkRoutingIdSlotAllocationMember {
  readonly channelName: string;
  readonly routingIdPrefix: string;
}

export interface ZLinkRoutingIdSlotAcquireRequest {
  readonly groupName: string;
  readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
  readonly slotCount: number;
  readonly ownerId: string;
  readonly leaseTtlMs: number;
}

export interface ZLinkRoutingIdSlotAllocation {
  readonly slot: number;
  readonly owner: ZLinkLocationOwnerToken;
  readonly leaseExpiresAt: Date;
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

결과는 group 이름, slot과 member 이름별 `RoutingId`를 제공한다. provider는 모든 socket bind와
location row 게시가 끝나 readiness에 도달한 뒤에만 완료된다. 등록되지 않은 group은
`ZLinkConfigurationException`으로 실패한다.

```ts
export interface ZLinkAllocatedRoutingId {
  readonly groupName: string;
  readonly slot: number;
  readonly memberRoutingIds: ReadonlyMap<string, RoutingId>;
}
```

## 4. location option

`ZLinkLocationOptions`는 기존 `heartbeatIntervalMs`, `ownerLeaseTtlMs`와 함께
`routingIdFencingMarginMs`, `ownerLeaseRenewTimeoutMs`를 제공한다. 기본값은 각각 10000, 30000,
5000, 3000이며 공통 계약의 시간 관계를 만족해야 한다.

## 회귀 테스트

| 테스트 | 확인 기준 |
|--------|-----------|
| builder contract | 네 builder가 같은 allocation 설정을 등록하고 고정 routing id 충돌을 거부한다 |
| in-memory store contract | 최소 slot, 멱등 acquire, 만료 후 재사용과 stale release fencing을 확인한다 |
| Redis store contract | Lua 원자 할당, group 구성 고정과 stale release 차단을 확인한다 |
| runtime lifecycle | bind 전 할당, readiness 이후 조회, bind 실패와 종료의 release를 확인한다 |
