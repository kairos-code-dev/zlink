# Node.js Location·Relocation provider 공개 interface

이 문서는 Node.js server package가 외부 provider와 application에 공개하는 Location·Relocation 계약만 고정한다. Framework runtime이 descriptor, lease, authority, reservation과 aggregate commit을 처리하기 위해 사용하는 세부 단계는 하나의 `ZLinkLocationStore` 안에 포함한다. 단계별 store interface와 저장 행·key는 공개하지 않는다.

이 문서는 provider가 구현해야 하는 DTO와 closed union의 단독 owner다. 다른 Node.js exact interface 문서는
아래 타입을 다시 선언하지 않고 이 문서를 참조한다.

## 0. Provider DTO와 value type

```ts
export interface ZLinkPageRequest { readonly pageSize?: number; readonly continuationToken?: string; }
export interface ZLinkLocationPage<T> { readonly items: readonly T[]; readonly continuationToken?: string; }

export enum ZLinkLocationWriteIntent { NewClaim = 1, Renew = 2, Takeover = 3 }
export enum ZLinkLocationWriteStatus {
  Stored = 'stored', IgnoredStale = 'ignoredStale', RejectedConflict = 'rejectedConflict'
}
export interface ZLinkLocationWriteResult {
  readonly status: ZLinkLocationWriteStatus; readonly generation: bigint; readonly updatedAt: Date;
}
export interface ZLinkLocationOwnerToken { readonly ownerId: string; readonly leaseGeneration: bigint; }
export type ZLinkOwnerLeaseClaimResult =
  | { readonly kind: 'claimed'; readonly token: ZLinkLocationOwnerToken; readonly leaseExpiresAt: Date; readonly storeNow: Date }
  | { readonly kind: 'conflict' }
  | { readonly kind: 'generationExhausted' };
export type ZLinkOwnerLeaseReadResult =
  | { readonly kind: 'found'; readonly token: ZLinkLocationOwnerToken; readonly leaseExpiresAt: Date; readonly storeNow: Date }
  | { readonly kind: 'missing' };
export type ZLinkOwnerLeaseRenewResult =
  | { readonly kind: 'renewed'; readonly leaseExpiresAt: Date; readonly storeNow: Date }
  | { readonly kind: 'stale' };
export type ZLinkOwnerLeaseReleaseResult = 'released' | 'stale';

export interface ZLinkPopulationCapacity { readonly active: number; readonly reserved: number; readonly limit: number; }
export interface ZLinkSpotTypeCapacity extends ZLinkPopulationCapacity {
  readonly objectKind: 'user_spot' | 'instance_spot'; readonly stableType: string;
}
export type ZLinkObjectMaintenancePolicyKind = 'disabled' | 'recreate' | 'snapshot';
export interface ZLinkObjectCapability {
  readonly objectKind: 'actor' | 'user_spot' | 'instance_spot';
  readonly stableType: string; readonly policy: ZLinkObjectMaintenancePolicyKind;
  readonly hasSnapshotAdapter: boolean; readonly limit: number;
}
export interface ZLinkMeshNodeDescriptorKey { readonly meshName: string; readonly rid: RoutingId; }
export interface ZLinkClientServerServerDescriptorKey { readonly channelName: string; readonly serverRid: RoutingId; }
export interface ZLinkFanoutPublisherDescriptorKey { readonly channelName: string; readonly publisherRid: RoutingId; }
export interface ZLinkMeshNodeDescriptor {
  readonly meshName: string; readonly rid: RoutingId; readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint; readonly endpoint: string; readonly objectRole: ZLinkObjectRole;
  readonly entrySpotId?: string; readonly placementWeight: number;
  readonly populationCapacity: { readonly actors: ZLinkPopulationCapacity; readonly spots: ZLinkPopulationCapacity; readonly spotTypes: readonly ZLinkSpotTypeCapacity[] };
  readonly activationConcurrency: { readonly active: number; readonly limit: number };
  readonly channelWeights: Readonly<Record<string, number>>; readonly applicationVersion: bigint;
  readonly spotTypes: readonly string[]; readonly objectCapabilities: readonly ZLinkObjectCapability[];
  readonly maintenanceWave?: string; readonly state: ZLinkFrameworkRuntimeState;
  readonly securityIdentity: string; readonly ownerId: string; readonly leaseGeneration: bigint; readonly updatedAt: Date;
}
export interface ZLinkClientServerServerDescriptor {
  readonly channelName: string; readonly serverRid: RoutingId; readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint; readonly endpoint: string; readonly weight: number;
  readonly state: ZLinkFrameworkRuntimeState; readonly securityIdentity: string;
  readonly ownerId: string; readonly leaseGeneration: bigint; readonly updatedAt: Date;
}
export interface ZLinkFanoutPublisherDescriptor {
  readonly channelName: string; readonly publisherRid: RoutingId; readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint; readonly endpoint: string; readonly state: ZLinkFrameworkRuntimeState;
  readonly securityIdentity: string; readonly ownerId: string; readonly leaseGeneration: bigint; readonly updatedAt: Date;
}

export interface ZLinkAuthorityKey { readonly value: string; readonly [zlinkAuthorityKeyBrand]: true; }
export interface ZLinkAuthorityStoreVersion { readonly value: string; readonly [zlinkAuthorityVersionBrand]: true; }
export type ZLinkPlacementObjectKind = 'actor' | 'user_spot' | 'instance_spot';
export type ZLinkPlacementAllocationState = 'reserved' | 'active';
export interface ZLinkSpotTypeCapacityDelta {
  readonly objectKind: 'user_spot' | 'instance_spot'; readonly stableType: string; readonly count: number;
}
export interface ZLinkCapacityVector { readonly actors: number; readonly spots: number; readonly spotType?: ZLinkSpotTypeCapacityDelta; }
export interface ZLinkPlacementAllocation {
  readonly state: ZLinkPlacementAllocationState; readonly objectKind: ZLinkPlacementObjectKind;
  readonly stableType: string; readonly descriptor: ZLinkMeshNodeDescriptorKey;
  readonly descriptorLifecycleGeneration: bigint; readonly capacity: ZLinkCapacityVector;
}
export interface ZLinkPendingObjectCreation {
  readonly reservationId: string; readonly requestContentReference: string;
  readonly requestSha256: Uint8Array; readonly requestEncodedSize: bigint;
}
export interface ZLinkAuthoritySnapshot {
  readonly kind: 'snapshot'; readonly storeVersion: ZLinkAuthorityStoreVersion; readonly payload: Uint8Array;
  readonly objectGeneration: bigint; readonly authorityOwnerGeneration: bigint;
  readonly ownerId: string; readonly ownerLeaseGeneration: bigint; readonly allocation: ZLinkPlacementAllocation;
  readonly pendingCreation?: ZLinkPendingObjectCreation; readonly storeNow: Date;
}
export type ZLinkAuthorityReadResult = { readonly kind: 'missing'; readonly storeNow: Date } | ZLinkAuthoritySnapshot;
export interface ZLinkRelocationCapacityFence { readonly value: string; readonly [zlinkRelocationCapacityFenceBrand]: true; }
export type ZLinkAuthorityMutation =
  | { readonly kind: 'put'; readonly payload: Uint8Array; readonly generationTransition: 'preserve' | 'newOwner'; readonly targetOwner?: ZLinkLocationOwnerToken; readonly relocationCapacityFence?: ZLinkRelocationCapacityFence }
  | { readonly kind: 'restore'; readonly payload: Uint8Array; readonly expectedOwner: ZLinkLocationOwnerToken }
  | { readonly kind: 'delete' };
export type ZLinkAuthorityCompareExchangeResult =
  | ({ readonly kind: 'stored' } & Omit<ZLinkAuthoritySnapshot, 'kind'>)
  | { readonly kind: 'deleted'; readonly storeVersion: ZLinkAuthorityStoreVersion; readonly storeNow: Date }
  | { readonly kind: 'conflict'; readonly current: ZLinkAuthorityReadResult }
  | { readonly kind: 'generationExhausted' };
export interface ZLinkAuthorityEntry { readonly key: ZLinkAuthorityKey; readonly snapshot: ZLinkAuthoritySnapshot; }
export class ZLinkAuthorityScanCursor { private constructor(readonly encoded: string); static from(encoded: string): ZLinkAuthorityScanCursor; }
export type ZLinkAuthorityScanResult =
  | { readonly kind: 'page'; readonly items: readonly ZLinkAuthorityEntry[]; readonly nextCursor?: ZLinkAuthorityScanCursor }
  | { readonly kind: 'scanExpired' };

export interface ZLinkObjectCreationKey { readonly kind: ZLinkPlacementObjectKind; readonly globalId: string; }
export interface ZLinkObjectCreationTarget {
  readonly meshName: string; readonly nodeRid: RoutingId; readonly nodeLifecycleGeneration: bigint;
  readonly owner: ZLinkLocationOwnerToken;
}
export interface ZLinkObjectCreationIntent {
  readonly stableType: string; readonly requestContentReference: string;
  readonly requestSha256: Uint8Array; readonly requestEncodedSize: bigint;
}
export interface ZLinkCreationOperationId { readonly high: bigint; readonly low: bigint; }
export interface ZLinkCreationOperationIdentity {
  readonly sourceNodeRid: RoutingId; readonly sourceNodeGeneration: bigint; readonly operationId: ZLinkCreationOperationId;
}
export interface ZLinkCreationTerminalPublication {
  readonly operation: ZLinkCreationOperationIdentity; readonly terminalEnvelope: Uint8Array;
  readonly terminalEnvelopeSha256: Uint8Array; readonly operationDeadline: Date;
}
export interface ZLinkCreationTerminalRecord {
  readonly state: 'created' | 'rejected' | 'failed'; readonly operation: ZLinkCreationOperationIdentity;
  readonly reservationId: string; readonly objectKind: ZLinkPlacementObjectKind;
  readonly terminalEnvelope: Uint8Array; readonly terminalEnvelopeSha256: Uint8Array;
  readonly expiresAt: Date; readonly storeNow: Date;
}
export type ZLinkCreationTerminalReadResult =
  | { readonly kind: 'missing'; readonly storeNow: Date }
  | { readonly kind: 'found'; readonly record: ZLinkCreationTerminalRecord };
export interface ZLinkObjectReserveRequest {
  readonly key: ZLinkObjectCreationKey; readonly intent: ZLinkObjectCreationIntent;
  readonly target: ZLinkObjectCreationTarget; readonly creatingPayload: Uint8Array; readonly capacity: ZLinkCapacityVector;
}
export type ZLinkObjectReserveResult =
  | { readonly kind: 'reserved'; readonly reservationId: string; readonly creating: ZLinkAuthoritySnapshot }
  | { readonly kind: 'alreadyExists' | 'typeMismatch'; readonly current: ZLinkAuthoritySnapshot }
  | { readonly kind: 'placementCapacityExhausted' }
  | { readonly kind: 'conflict'; readonly current: ZLinkAuthorityReadResult }
  | { readonly kind: 'generationExhausted' };
export interface ZLinkObjectCommitRequest {
  readonly key: ZLinkObjectCreationKey; readonly reservationId: string; readonly expectedStoreVersion: string;
  readonly target: ZLinkObjectCreationTarget; readonly readyPayload: Uint8Array;
}
export type ZLinkObjectCommitResult =
  | { readonly kind: 'committed' | 'alreadyCommitted'; readonly ready: ZLinkAuthoritySnapshot }
  | { readonly kind: 'stale' | 'generationExhausted' };
export type ZLinkObjectCreationCompletion =
  | { readonly kind: 'created'; readonly readyPayload: Uint8Array; readonly terminal: ZLinkCreationTerminalPublication }
  | { readonly kind: 'rejected' | 'failed'; readonly terminal: ZLinkCreationTerminalPublication };
export interface ZLinkObjectCreationCompleteRequest {
  readonly key: ZLinkObjectCreationKey; readonly reservationId: string; readonly expectedStoreVersion: string;
  readonly target: ZLinkObjectCreationTarget; readonly completion: ZLinkObjectCreationCompletion;
}
export type ZLinkObjectCreationCompleteResult =
  | { readonly kind: 'created'; readonly ready: ZLinkAuthoritySnapshot; readonly terminal: ZLinkCreationTerminalRecord }
  | { readonly kind: 'rejected' | 'failed' | 'alreadyCompleted'; readonly terminal: ZLinkCreationTerminalRecord }
  | { readonly kind: 'stale' | 'generationExhausted' };
export interface ZLinkObjectAbortRequest {
  readonly key: ZLinkObjectCreationKey; readonly reservationId: string;
  readonly expectedStoreVersion: string; readonly target: ZLinkObjectCreationTarget;
}
export type ZLinkObjectAbortResult = { readonly kind: 'aborted' | 'alreadyAborted' | 'stale' };

export interface ZLinkRelocationCapacityReservationRequest {
  readonly reservationId: string; readonly authorityKey: ZLinkAuthorityKey;
  readonly expectedStoreVersion: ZLinkAuthorityStoreVersion; readonly objectKind: ZLinkPlacementObjectKind;
  readonly stableType: string; readonly sourceDescriptor: ZLinkMeshNodeDescriptorKey;
  readonly sourceNodeLifecycleGeneration: bigint; readonly sourceOwner: ZLinkLocationOwnerToken;
  readonly targetDescriptor: ZLinkMeshNodeDescriptorKey; readonly targetNodeLifecycleGeneration: bigint;
  readonly targetOwner: ZLinkLocationOwnerToken; readonly capacity: ZLinkCapacityVector;
}
export type ZLinkRelocationCapacityReserveResult =
  | { readonly kind: 'reserved' | 'alreadyReserved'; readonly fence: ZLinkRelocationCapacityFence }
  | { readonly kind: 'conflict'; readonly current: ZLinkAuthorityReadResult }
  | { readonly kind: 'targetUnavailable' | 'placementCapacityExhausted' };
export type ZLinkRelocationCapacityAbortResult = 'aborted' | 'alreadyAborted' | 'alreadyCommitted' | 'stale';
export interface ZLinkAggregateId { readonly value: string; readonly [zlinkAggregateIdBrand]: true; }
export interface ZLinkAggregateParticipant {
  readonly authorityKey: ZLinkAuthorityKey; readonly expectedStoreVersion: ZLinkAuthorityStoreVersion;
  readonly ownerTransition: 'preserve' | 'newOwner'; readonly authorityPayload: Uint8Array;
  readonly membershipMutation: Uint8Array;
}
export interface ZLinkAggregatePrepareRequest {
  readonly aggregateId: ZLinkAggregateId; readonly aggregateGeneration: bigint;
  readonly participants: readonly ZLinkAggregateParticipant[]; readonly inventoryDigest: Uint8Array;
  readonly targetDescriptor: ZLinkMeshNodeDescriptorKey; readonly targetDescriptorLifecycleGeneration: bigint;
  readonly capacity: ZLinkCapacityVector; readonly targetOwner: ZLinkLocationOwnerToken;
}
export interface ZLinkAggregateFence { readonly aggregateId: ZLinkAggregateId; readonly aggregateGeneration: bigint; }
export type ZLinkAggregatePrepareResult =
  | { readonly kind: 'prepared' | 'alreadyPrepared'; readonly fence: ZLinkAggregateFence }
  | { readonly kind: 'conflict' | 'stale' | 'generationExhausted' };
export type ZLinkAggregateCommitResult = { readonly kind: 'committed' | 'alreadyCommitted' | 'stale' | 'generationExhausted' };
export type ZLinkAggregateAbortResult = { readonly kind: 'aborted' | 'alreadyAborted' | 'stale' };

export interface ZLinkRelocationReference { readonly value: string; readonly [zlinkRelocationReferenceBrand]: true; }
export interface ZLinkRelocationStored {
  readonly reference: ZLinkRelocationReference; readonly checksumCrc32c: number;
  readonly expiresAt: Date; readonly storeNow: Date;
}
export type ZLinkRelocationReadResult =
  | { readonly kind: 'found'; readonly payload: Uint8Array }
  | { readonly kind: 'missing' };
export type ZLinkRelocationRenewResult =
  | { readonly kind: 'renewed'; readonly expiresAt: Date; readonly storeNow: Date }
  | { readonly kind: 'missing' };
export type ZLinkRelocationDeleteResult = 'deleted' | 'missing';
```

## 1. 공개 provider SPI

```ts
export interface ZLinkLocationStore {
  updateMeshNode(
    descriptor: ZLinkMeshNodeDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;

  removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus>;

  listMeshNodes(
    meshName: string,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkMeshNodeDescriptor>>;

  updateClientServer(
    descriptor: ZLinkClientServerServerDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;

  removeClientServer(
    key: ZLinkClientServerServerDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus>;

  listClientServers(
    channelName: string,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkClientServerServerDescriptor>>;

  updateFanoutPublisher(
    descriptor: ZLinkFanoutPublisherDescriptor,
    intent: ZLinkLocationWriteIntent,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteResult>;

  removeFanoutPublisher(
    key: ZLinkFanoutPublisherDescriptorKey,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkLocationWriteStatus>;

  listFanoutPublishers(
    channelName: string,
    page?: ZLinkPageRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>>;

  claimOwnerLease(ownerId: string, leaseTtlMs: number, signal?: AbortSignal): Promise<ZLinkOwnerLeaseClaimResult>;
  readOwnerLease(ownerId: string, signal?: AbortSignal): Promise<ZLinkOwnerLeaseReadResult>;
  renewOwnerLease(owner: ZLinkLocationOwnerToken, leaseTtlMs: number, signal?: AbortSignal): Promise<ZLinkOwnerLeaseRenewResult>;
  releaseOwnerLease(owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<ZLinkOwnerLeaseReleaseResult>;

  readAuthority(key: ZLinkAuthorityKey, signal?: AbortSignal): Promise<ZLinkAuthorityReadResult>;
  compareExchangeAuthority(
    key: ZLinkAuthorityKey,
    expectedStoreVersion: ZLinkAuthorityStoreVersion,
    mutation: ZLinkAuthorityMutation,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityCompareExchangeResult>;
  listAuthorities(
    prefix: string,
    cursor: ZLinkAuthorityScanCursor | undefined,
    limit: number,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityScanResult>;

  readCreationTerminal(operation: ZLinkCreationOperationIdentity, signal?: AbortSignal): Promise<ZLinkCreationTerminalReadResult>;
  reserve(request: ZLinkObjectReserveRequest, signal?: AbortSignal): Promise<ZLinkObjectReserveResult>;
  commit(request: ZLinkObjectCommitRequest, signal?: AbortSignal): Promise<ZLinkObjectCommitResult>;
  completeCreation(request: ZLinkObjectCreationCompleteRequest, signal?: AbortSignal): Promise<ZLinkObjectCreationCompleteResult>;
  abort(request: ZLinkObjectAbortRequest, signal?: AbortSignal): Promise<ZLinkObjectAbortResult>;

  reserveRelocationCapacity(request: ZLinkRelocationCapacityReservationRequest, signal?: AbortSignal): Promise<ZLinkRelocationCapacityReserveResult>;
  abortRelocationCapacity(fence: ZLinkRelocationCapacityFence, signal?: AbortSignal): Promise<ZLinkRelocationCapacityAbortResult>;
  prepareAggregate(request: ZLinkAggregatePrepareRequest, signal?: AbortSignal): Promise<ZLinkAggregatePrepareResult>;
  commitAggregate(fence: ZLinkAggregateFence, signal?: AbortSignal): Promise<ZLinkAggregateCommitResult>;
  abortAggregate(fence: ZLinkAggregateFence, signal?: AbortSignal): Promise<ZLinkAggregateAbortResult>;

  removeAllByOwner(owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<bigint>;

  // Descriptor refresh를 즉시 깨우는 선택적 hint다. 없으면 runtime이 polling한다.
  getMeshNodeChangeStamp?(meshName: string, signal?: AbortSignal): Promise<bigint | undefined>;
}

export interface ZLinkRelocationStore {
  putRelocation(payload: Uint8Array, retentionMs: number, signal?: AbortSignal): Promise<ZLinkRelocationStored>;
  getRelocation(reference: ZLinkRelocationReference, signal?: AbortSignal): Promise<ZLinkRelocationReadResult>;
  renewRelocation(reference: ZLinkRelocationReference, retentionMs: number, signal?: AbortSignal): Promise<ZLinkRelocationRenewResult>;
  deleteRelocation(reference: ZLinkRelocationReference, signal?: AbortSignal): Promise<ZLinkRelocationDeleteResult>;
}
```

`ZLinkLocationStore`가 descriptor·lease·authority·reservation·aggregate commit의 transaction boundary를 소유한다. `ZLinkRelocationStore`는 immutable relocation payload만 저장한다. 두 store 사이의 공개 distributed transaction 계약은 없다.

`ZLinkAuthorityMutation`의 `restore` variant는 startup recovery 전용이다. Provider는 exact
`expectedStoreVersion`, active allocation과 `expectedOwner`를 함께 확인하지만 owner lease가 현재 live인지는
요구하지 않는다. 따라서 payload root를 게시하기 전에 process가 종료되어 남은 root 없는 `Preparing`
authority만 이전 steady payload로 복원할 수 있다. 일반 `put`과 `delete`의 live-owner fence는 그대로 유지한다.

다음 이름은 public contract가 아니다.

- capability별 `*Store` interface
- `Watch`·change event store
- Actor 전용 transfer store
- Routing ID allocation slot store
- Spot·Actor location 저장 행과 key

## 2. Redis extension 공개 표면

```ts
export interface ZLinkRedisLocationOptions {
  readonly url?: string;
  readonly client?: RedisClientType;
  readonly clientOptions?: RedisClientOptions;
  readonly keyPrefix: string;
}

export interface ZLinkRedisRelocationOptions {
  readonly url?: string;
  readonly client?: RedisClientType;
  readonly clientOptions?: RedisClientOptions;
  readonly keyPrefix: string;
}

export class ZLinkRedisLocationStore implements ZLinkLocationStore {
  constructor(options: ZLinkRedisLocationOptions);
  dispose(): Promise<void>;
}

export class ZLinkRedisRelocationStore implements ZLinkRelocationStore {
  constructor(options: ZLinkRedisRelocationOptions);
  dispose(): Promise<void>;
}
```

공개 extension 표면은 options 두 개와 store 구현 두 개다. raw Redis command adapter, mutable options builder와 Lua script·key codec은 extension 내부 구현이다.

같은 Redis deployment를 사용하려면 두 options에 같은 URL 또는 client를 넘기고 서로 다른 `keyPrefix`를 사용한다. 물리적으로 분리할 때도 application의 Framework 등록 계약은 바뀌지 않는다.

## 3. 운영 query

Application이 저장 행을 직접 조합하지 않도록 runtime query는 aggregate projection만 제공한다.

`ZLinkLocationRuntimeQuery`의 exact 선언은
[Location 운영 조회와 observability](03-location-observability.ko.md)가 단독으로 소유한다. 이 문서는
provider가 공급하는 저장 capability와 relocation 책임만 고정한다.

`ZLinkLocationAutoConnectType`, raw Spot·Actor·route row query와 저장 key는 runtime 내부 계약이므로 이 문서와 package root export에 포함하지 않는다.
