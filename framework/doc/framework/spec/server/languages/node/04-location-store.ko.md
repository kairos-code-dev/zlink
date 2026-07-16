# Node.js Location Store·Redis 공개 인터페이스

[Node.js 계약 목차](README.ko.md) · [Location Runtime](../../40-location-runtime.ko.md) ·
[Redis Location Store](../../41-location-store-redis.ko.md)

## 1. Root 등록과 option

```ts
export interface ZLinkFrameworkOptions {
    addLocationStore(store: ZLinkLocationStore): this;
    configureLocations(): ZLinkLocationOptions;
}

export interface ZLinkLocationOptions {
    heartbeatIntervalMs(value: number): this;
    ownerLeaseTtlMs(value: number): this;
    pollingIntervalMs(value: number): this;
    storeFailureGraceMs(value: number): this;
    routingIdFencingMarginMs(value: number): this;
    ownerLeaseRenewTimeoutMs(value: number): this;
}
```

`zlinkDefaultLocationOptions`는 setter를 가진 `ZLinkLocationOptions`가 아니라 여섯 기본값을 담은
`Readonly<ZLinkLocationOptionValues>`다. 따라서 기본값 조회와 option builder의 변경 연산을 같은 타입으로
혼동하지 않는다.

Store는 application마다 하나만 등록한다. 자동 discovery, remote Spot·Actor 위치, routing ID 자동 할당 또는
Actor transfer를 설정했는데 store나 필요한 capability가 없으면 NestJS module 초기화는 socket bind 전에
구성 오류로 종료한다.

여섯 duration은 모두 양수여야 한다. Routing ID 자동 할당을 사용할 때 heartbeat, renew timeout, lease
TTL과 fencing margin은 [Location Runtime §2.4](../../40-location-runtime.ko.md#24-owner-lease)의 시간 관계를
만족한다. 기본값은 각각 10000, 30000, 1000, 30000, 5000과 3000밀리초다.

## 2. Store-neutral record와 capability

```ts
export type ZLinkLocationWriteIntent = "newClaim" | "renew" | "takeover";
export type ZLinkLocationWriteStatus = "stored" | "ignoredStale" | "rejectedConflict";

export interface ZLinkLocationWriteResult {
    readonly status: ZLinkLocationWriteStatus;
    readonly generation: bigint;
    readonly updatedAt: Date;
}
export interface ZLinkLocationOwnerToken { readonly ownerId: string; readonly generation: bigint; }
export interface ZLinkOwnerLease {
    readonly ownerId: string;
    readonly nodeRid: RoutingId;
    readonly leaseExpiresAt: Date;
    readonly updatedAt: Date;
}
export interface ZLinkOwnerLeaseRenewal { readonly leaseExpiresAt: Date; readonly storeNow: Date; }
export interface ZLinkOwnerLeaseSnapshot {
    readonly leases: readonly ZLinkOwnerLease[];
    readonly storeNow: Date;
}

export interface ZLinkMeshNodeDescriptor {
    readonly meshName: string;
    readonly rid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly channelWeights: Readonly<Record<string, number>>;
    readonly draining: boolean;
    readonly securityIdentity: string;
    readonly ownerId: string;
    readonly updatedAt: Date;
}
export interface ZLinkMeshNodeDescriptorKey { readonly meshName: string; readonly rid: RoutingId; }

export interface ZLinkSpotLocation {
    readonly meshName: string;
    readonly spotRid: RoutingId;
    readonly spotGeneration: bigint;
    readonly ownerNodeRid: RoutingId;
    readonly ownerNodeGeneration: bigint;
    readonly spotKind: ZLinkSpotKind;
    readonly spotType: string;
    readonly ownerId: string;
    readonly updatedAt: Date;
}
export interface ZLinkSpotLocationKey { readonly meshName: string; readonly spotRid: RoutingId; }

export interface ZLinkActorLocation {
    readonly meshName: string;
    readonly actorId: string;
    readonly actorType: string;
    readonly actorRef: ActorRef;
    readonly ownerNodeRid: RoutingId;
    readonly ownerNodeGeneration: bigint;
    readonly spotRid: RoutingId;
    readonly spotKind: ZLinkSpotKind;
    readonly membershipEpoch: bigint;
    readonly ownerId: string;
    readonly updatedAt: Date;
}
export interface ZLinkActorLocationKey { readonly meshName: string; readonly actorId: string; }

export interface ZLinkMeshNodeLocationStore {
    updateMeshNode(descriptor: ZLinkMeshNodeDescriptor, intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeMeshNode(key: ZLinkMeshNodeDescriptorKey, owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    listMeshNodes(meshName: string, signal?: AbortSignal): Promise<readonly ZLinkMeshNodeDescriptor[]>;
}
export interface ZLinkSpotLocationStore {
    updateSpot(location: ZLinkSpotLocation, intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeSpot(key: ZLinkSpotLocationKey, owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    resolveSpot(key: ZLinkSpotLocationKey, signal?: AbortSignal): Promise<ZLinkSpotLocation | undefined>;
}
export interface ZLinkActorLocationStore {
    updateActor(location: ZLinkActorLocation, intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeActor(key: ZLinkActorLocationKey, owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    resolveActor(key: ZLinkActorLocationKey, signal?: AbortSignal): Promise<ZLinkActorLocation | undefined>;
}
export interface ZLinkOwnerLeaseStore {
    renewOwnerLease(ownerId: string, nodeRid: RoutingId, leaseTtlMs: number,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseRenewal>;
    removeOwnerLease(ownerId: string, signal?: AbortSignal): Promise<boolean>;
    listOwnerLeases(signal?: AbortSignal): Promise<ZLinkOwnerLeaseSnapshot>;
}
```

## 3. Actor transfer authority

```ts
export type ZLinkActorTransferState = "prepared" | "committed" | "activated" | "aborted";
export type ZLinkActorTransferWriteStatus =
    | "stored" | "notFound" | "ignoredStale" | "rejectedConflict" | "invalidState";

export interface ZLinkActorTransferRecord {
    readonly meshName: string;
    readonly actorId: string;
    readonly transferId: string;
    readonly source: ActorRef;
    readonly target: ActorRef;
    readonly expectedActorGeneration: bigint;
    readonly expectedMembershipEpoch: bigint;
    readonly participants: ReadonlySet<RoutingId>;
    readonly state: ZLinkActorTransferState;
    readonly recoveryOwnerId: string;
    readonly recoveryLeaseExpiresAt: Date;
    readonly updatedAt: Date;
}
export interface ZLinkActorTransferPrepareRequest {
    readonly meshName: string;
    readonly actorId: string;
    readonly transferId: string;
    readonly source: ActorRef;
    readonly target: ActorRef;
    readonly expectedActorGeneration: bigint;
    readonly expectedMembershipEpoch: bigint;
    readonly participants: ReadonlySet<RoutingId>;
    readonly recoveryOwnerId: string;
    readonly recoveryLeaseTtlMs: number;
}
export interface ZLinkActorTransferWriteResult {
    readonly status: ZLinkActorTransferWriteStatus;
    readonly record?: ZLinkActorTransferRecord;
}
export interface ZLinkActorTransferStore {
    prepareActorTransfer(request: ZLinkActorTransferPrepareRequest,
        signal?: AbortSignal): Promise<ZLinkActorTransferWriteResult>;
    commitActorTransfer(meshName: string, actorId: string, transferId: string,
        recoveryOwnerId: string, signal?: AbortSignal): Promise<ZLinkActorTransferWriteResult>;
    activateActorTransfer(meshName: string, actorId: string, transferId: string,
        recoveryOwnerId: string, signal?: AbortSignal): Promise<ZLinkActorTransferWriteResult>;
    abortActorTransfer(meshName: string, actorId: string, transferId: string,
        recoveryOwnerId: string, signal?: AbortSignal): Promise<ZLinkActorTransferWriteResult>;
    takeOverActorTransfer(meshName: string, actorId: string, transferId: string,
        successorOwnerId: string, recoveryLeaseTtlMs: number,
        signal?: AbortSignal): Promise<ZLinkActorTransferWriteResult>;
    resolveActorTransfer(meshName: string, actorId: string,
        signal?: AbortSignal): Promise<ZLinkActorTransferRecord | undefined>;
}
export interface ZLinkLocationStore extends
    ZLinkMeshNodeLocationStore,
    ZLinkSpotLocationStore,
    ZLinkActorLocationStore,
    ZLinkOwnerLeaseStore,
    ZLinkActorTransferStore {
    removeAllByOwner(ownerId: string, signal?: AbortSignal): Promise<bigint>;
}
```

Prepare는 active transfer 부재, Actor generation과 membership epoch를 한 원자 operation에서 비교한다.
Commit은 target owner와 정확히 다음 membership epoch를 함께 기록한다. Takeover는 recovery lease 만료,
participant set과 현재 Actor location을 같은 operation에서 확인한다.
`transferId`는 UUID 128-bit를 소문자 `8-4-4-4-12` 문자열로 표현한 값만 허용한다. 다른 UUID 표기나 임의
문자열은 store operation을 시작하기 전에 거부한다.

## 4. 공식 Redis package

```ts
export interface ZLinkRedisLocationOptions {
    readonly url: string;
    readonly keyPrefix: string;
}

export declare class ZLinkRedisLocationStore implements
    ZLinkLocationStore,
    ZLinkRoutingIdSlotAllocationStore,
    ZLinkLocationChangeStampStore {
    constructor(options: ZLinkRedisLocationOptions);
    close(): Promise<void>;
}
```

위 타입은 `@zlink-systems/framework-locations-redis`가 export한다. `url`과 비어 있지 않은 `keyPrefix`는
필수다. Store가 Redis client를 소유하며 `close()`가 시작된 뒤 새 operation은 closed-store 오류로 실패한다.
