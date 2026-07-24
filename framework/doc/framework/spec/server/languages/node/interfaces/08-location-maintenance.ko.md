# Node.js Location Store와 maintenance 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Location Runtime](../../../40-location-runtime.ko.md) ·
[Redis Location Store](../../../41-location-store-redis.ko.md)

이 문서는 Node.js Framework가 location discovery, owner authority와 relocation payload 보관에 사용하는 정확한
TypeScript 인터페이스를 고정한다. Store provider는 authority와 relocation payload를 해석하지 않는다.
Application service code는 provider operation을 직접 호출하지 않고 root 구성에서 provider만 등록한다.

## 1. Root 등록과 option

```ts
export interface ZLinkLocationOptions {
    ownerLeaseRenewIntervalMs(value: number): this;
    ownerLeaseTtlMs(value: number): this;
    pollingIntervalMs(value: number): this;
    storeFailureGraceMs(value: number): this;
    ownerLeaseFencingMarginMs(value: number): this;
    ownerLeaseRenewTimeoutMs(value: number): this;
    routeCacheMaxAgeMs(value: number): this;
    relocationForwardingWindowMs(value: number): this;
    maxActiveOutboundRelocations(value: number): this;
    maxActiveInboundRelocations(value: number): this;
    maxConcurrentRelocationCaptures(value: number): this;
    maxConcurrentRelocationRestores(value: number): this;
    maxRelocationPayloadInFlightBytes(value: number): this;
}
```

Location runtime을 사용하는 application은 Location Store를 정확히 하나 등록한다. `Recreate` 또는 `Snapshot`
factory가 하나라도 있거나 Instance Spot factory가 하나라도 있으면 opaque state, accepted journal, full
inventory와 replay payload를 보존하는 Relocation Store도 정확히 하나 등록한다. Instance Spot factory가 없고
`Disabled` factory만 있는 same-node 구성만 이를 생략할 수 있다. 두 Store는 별도 객체와 별도 registration이다.
필요한 Store가 없거나 같은 capability가 중복 등록되면 Framework는 socket bind 전에 구성 오류로 종료한다.

완료 가능한 모든 cross-node Actor·Spot 이동은 Relocation Store를 사용한다. `Recreate`도 accepted journal과
recovery payload를 저장하며 `Snapshot`은 application state를 추가로 저장한다. Same-node Actor join은 Relocation
payload를 만들지 않고, `Disabled` cross-node 이동은 capture 전에 거부한다.

앞의 여섯 lease·polling duration은 모두 양수여야 하고 route cache와 relocation forwarding window는 0 이상이다.
기본값은 선언 순서대로 5000, 15000, 1000, 30000, 5000, 3000, 15000, 30000밀리초다.
`routeCacheMaxAgeMs`와 `relocationForwardingWindowMs`가 모두 양수이면 cache age가 forwarding window보다 최소
5000밀리초 작아야 한다. `ownerLeaseRenewIntervalMs`의 첫 번째 값은 Store owner lease 갱신
주기이며 service connection의 liveness interval이 아니다.
Relocation 제한의 기본값은 active outbound 64, active inbound 64, concurrent Capture 8, concurrent Restore 8,
encoded payload in flight 268,435,456 bytes다. 다섯 값은 모두 `Number.isSafeInteger(value) && value > 0`을
만족해야 하며 `NaN`, infinity와 fraction은 socket bind 전 configuration error다. 같은 process의 모든 MeshNode가
값을 공유한다. Framework는 active unit, callback과 byte permit을 모두 얻기 전에는 source queue를 seal하지 않는다.
Byte 한도를 넘는 단일 User Spot aggregate는 다른 relocation payload 단계와 겹치지 않는 조건으로 단독 실행한다.
Location Store와 owner lease runtime을 사용하는 모든 host는
`ownerLeaseRenewIntervalMs + ownerLeaseRenewTimeoutMs < ownerLeaseTtlMs - ownerLeaseFencingMarginMs`를
startup에서 검증한다.

`storeFailureGraceMs`는 discovery reconcile과 새 outbound connect에만 적용한다. Store failure 동안 마지막 stable
desired set을 grace까지 고정하고 existing admitted transport에는 service liveness를 계속 적용한다. Grace 뒤에는
stable store snapshot을 다시 얻기 전까지 새 connection을 만들지 않는다. 이 값은 owner·coordinator lease나 local
authority deadline을 연장하지 않으며 stateful message, timer, factory와 CAS admission은 마지막 valid monotonic
lease deadline에서 닫힌다. Recovery는 exact owner token과 stable page set을 재검증한 뒤 diff와 connect를 수행한다.

Object role이 `none`인 MeshNode는 manual routing만 제공하며 Actor·Spot manager와 factory를 제공하지 않는다.
Object role이 `client` 또는 `server`이면 global authority를 위해 Location Store가 필요하다.

## 2. Discovery와 일반 location record

```ts
export enum ZLinkLocationWriteIntent {
    NewClaim = 1,
    Renew = 2,
    Takeover = 3
}

export enum ZLinkLocationWriteStatus {
    Stored = "stored",
    IgnoredStale = "ignoredStale",
    RejectedConflict = "rejectedConflict"
}

export interface ZLinkLocationWriteResult {
    readonly status: ZLinkLocationWriteStatus;
    readonly generation: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkLocationOwnerToken {
    readonly ownerId: string;
    readonly leaseGeneration: bigint;
}

export type ZLinkOwnerLeaseClaimResult =
    | {
        readonly kind: "claimed";
        readonly token: ZLinkLocationOwnerToken;
        readonly leaseExpiresAt: Date;
        readonly storeNow: Date;
    }
    | { readonly kind: "conflict" }
    | { readonly kind: "generationExhausted" };

export type ZLinkOwnerLeaseRenewResult =
    | {
        readonly kind: "renewed";
        readonly leaseExpiresAt: Date;
        readonly storeNow: Date;
    }
    | { readonly kind: "stale" };

export type ZLinkOwnerLeaseReleaseResult = "released" | "stale";

export type ZLinkOwnerLeaseReadResult =
    | {
        readonly kind: "found";
        readonly token: ZLinkLocationOwnerToken;
        readonly leaseExpiresAt: Date;
        readonly storeNow: Date;
    }
    | { readonly kind: "missing" };

export type ZLinkPlacementObjectKind = "actor" | "user_spot" | "instance_spot";
export type ZLinkObjectMaintenancePolicyKind =
    "disabled" | "recreate" | "snapshot";

export interface ZLinkObjectCapability {
    readonly objectKind: ZLinkPlacementObjectKind;
    readonly stableType: string;
    readonly policy: ZLinkObjectMaintenancePolicyKind;
    readonly hasSnapshotAdapter: boolean;
    readonly limit: number;
}

export interface ZLinkPopulationCapacity {
    readonly active: number;
    readonly reserved: number;
    readonly limit: number;
}

export interface ZLinkSpotTypeCapacity extends ZLinkPopulationCapacity {
    readonly objectKind: "user_spot" | "instance_spot";
    readonly stableType: string;
}

export interface ZLinkMeshNodeDescriptor {
    readonly meshName: string;
    readonly rid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly objectRole: ZLinkObjectRole;
    readonly entrySpotId?: SpotId;
    readonly placementWeight: number;
    readonly populationCapacity: {
        readonly actors: ZLinkPopulationCapacity;
        readonly spots: ZLinkPopulationCapacity;
        readonly spotTypes: readonly ZLinkSpotTypeCapacity[];
    };
    readonly activationConcurrency: {
        readonly active: number;
        readonly limit: number;
    };
    readonly channelWeights: Readonly<Record<string, number>>;
    readonly applicationVersion: bigint;
    readonly spotTypes: readonly string[];
    readonly objectCapabilities: readonly ZLinkObjectCapability[];
    readonly maintenanceWave?: string;
    readonly state: ZLinkFrameworkRuntimeState;
    readonly securityIdentity: string;
    readonly ownerId: string;
    readonly leaseGeneration: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkMeshNodeDescriptorKey {
    readonly meshName: string;
    readonly rid: RoutingId;
}

export interface ZLinkClientServerServerDescriptor {
    readonly channelName: string;
    readonly serverRid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly weight: number;
    readonly state: ZLinkFrameworkRuntimeState;
    readonly securityIdentity: string;
    readonly ownerId: string;
    readonly leaseGeneration: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkClientServerServerDescriptorKey {
    readonly channelName: string;
    readonly serverRid: RoutingId;
}

export interface ZLinkFanoutPublisherDescriptor {
    readonly channelName: string;
    readonly publisherRid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly state: ZLinkFrameworkRuntimeState;
    readonly securityIdentity: string;
    readonly ownerId: string;
    readonly leaseGeneration: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkFanoutPublisherDescriptorKey {
    readonly channelName: string;
    readonly publisherRid: RoutingId;
}

export interface ZLinkSpotLocation {
    readonly meshName: string;
    readonly spotId: SpotId;
    readonly spotGeneration: bigint;
    readonly ownerNodeRid: RoutingId;
    readonly ownerNodeGeneration: bigint;
    readonly spotKind: ZLinkSpotKind;
    readonly spotType: string;
    readonly ownerId: string;
    readonly leaseGeneration: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkSpotLocationKey {
    readonly spotId: SpotId;
}

export interface ZLinkActorLocation {
    readonly meshName: string;
    readonly actorId: string;
    readonly actorType: string;
    readonly actorRef: ActorRef;
    readonly ownerNodeRid: RoutingId;
    readonly ownerNodeGeneration: bigint;
    readonly spotKind: ZLinkSpotKind;
    readonly spotId: SpotId;
    readonly spotGeneration: bigint;
    readonly ownerId: string;
    readonly leaseGeneration: bigint;
    readonly updatedAt: Date;
}

export interface ZLinkActorLocationKey {
    readonly actorId: ActorId;
}

export interface ZLinkMeshNodeLocationStore {
    updateMeshNode(descriptor: ZLinkMeshNodeDescriptor, intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeMeshNode(key: ZLinkMeshNodeDescriptorKey, owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    listMeshNodes(meshName: string, page?: ZLinkPageRequest,
        signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkMeshNodeDescriptor>>;
}

export interface ZLinkClientServerLocationStore {
    updateClientServer(descriptor: ZLinkClientServerServerDescriptor,
        intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeClientServer(key: ZLinkClientServerServerDescriptorKey,
        owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    listClientServers(channelName: string, page?: ZLinkPageRequest,
        signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkClientServerServerDescriptor>>;
}

export interface ZLinkFanoutLocationStore {
    updateFanoutPublisher(descriptor: ZLinkFanoutPublisherDescriptor,
        intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeFanoutPublisher(key: ZLinkFanoutPublisherDescriptorKey,
        owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    listFanoutPublishers(channelName: string, page?: ZLinkPageRequest,
        signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>>;
}

export interface ZLinkOwnerLeaseStore {
    claimOwnerLease(ownerId: string, leaseTtlMs: number,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseClaimResult>;
    readOwnerLease(ownerId: string,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseReadResult>;
    renewOwnerLease(token: ZLinkLocationOwnerToken, leaseTtlMs: number,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseRenewResult>;
    releaseOwnerLease(token: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseReleaseResult>;
}
```

`entrySpotId`는 Object Server descriptor에서만 값이 있으며 Runtime이 해당 lifecycle에 발급한 exact Entry
Spot ID다. Actor placement·join과 relocation은 `rid`, `lifecycleGeneration`, `entrySpotId` mapping을
그대로 사용하며 문자열 관계를 추론하지 않는다. Object Client와 object role이 `none`인 descriptor에서는
생략한다.

기존 `updateMeshNode(descriptor, ZLinkLocationWriteIntent.NewClaim)`은 Object Server descriptor identity와
`entrySpotId` global Spot identity를 exact owner lease·lifecycle로 한 transaction에서 claim한다. 별도 Entry
claim public method는 제공하지 않는다. 어느 identity라도 active owner와 충돌하면 descriptor, Entry claim과
index를 바꾸지 않는다. `entrySpotId`는 descriptor immutable field와 digest에 포함되며 renew나 mutable
update로 바꿀 수 없다.

`removeMeshNode`와 `removeAllByOwner`는 exact owner lease·lifecycle이 일치할 때만 Entry claim을 descriptor와
함께 해제한다. Stale cleanup은 successor descriptor나 Entry claim을 제거할 수 없다. User Spot과 Instance
Spot의 generic `reserve`는 같은 global identity claim과 충돌하는 RID를 거부하며 authority나 capacity를 일부
변경하지 않는다. Contract test는 두 identity claim의 원자성, 충돌 무변경, exact cleanup, stale successor
보호와 immutable digest를 검증한다.

`applicationVersion`은 `0..9223372036854775807` 범위의 `bigint`다. `objectCapabilities`는 Actor와 User·Instance
Spot을 object kind와 stable type별로 구분하고 policy와 Snapshot adapter 등록 여부를 같은
항목에 둔다. User·Instance Spot의 `limit`은 stable type별 limit이고 Actor는 `0`이다.
`hasSnapshotAdapter`는 해당 kind adapter의 등록 여부만 나타내며 state format,
version이나 contract ID를 싣지 않는다. Snapshot policy이면 이 값은 `true`여야 하고 Disabled·Recreate이면
`false`다. `populationCapacity`는 Actor 전체, Spot 전체와 등록한 Spot type별 active·reserved·limit을 구분한다.
`activationConcurrency`는 population capacity와 별도인 process-local active·limit이다. Runtime state,
current capacity, activation active count, maintenance wave 또는 weight가 바뀌면 descriptor
revision을 증가시킨다. ClientServer와 fanout은 RouteMesh descriptor의 key나 store operation을 재사용하지
않는다.

Descriptor의 key, RID, lifecycle generation, endpoint, security identity, owner token, application version,
ChannelName key set, Spot type set와 object capability의 kind·stable type·policy·Snapshot adapter 등록 여부·
type별 limit은 첫 admission 뒤
해당 lifecycle에서 바뀌지 않는다. Population·Spot type limit과 activation concurrency limit도 immutable하다.
Channel weight 값, placement weight, current `populationCapacity`, activation active count, maintenance wave와 runtime state만
mutable하다. Mutable update는 current owner token과 같은 lifecycle generation을 제시하고
`descriptorRevision`을 strictly 증가시켜야 한다. Provider는 stale revision이나 immutable field 변경을 원자적으로
거부하며 일부 field만 적용하지 않는다. ClientServer와 fanout descriptor도 같은 identity·revision fence를
적용한다.

Channel weight, placement weight와 ClientServer descriptor weight는 integer `number` `0..10000`이다.
Provider는 범위 밖 값, fraction, `NaN`과 무한대를 거부하며 값을 truncate하지 않는다.

`lifecycleGeneration`은 0이 아닌 opaque equality token이다. Runtime은 수치 크기로 lifecycle의 선후를
판정하지 않는다. Store-backed descriptor에는 exact owner lease·descriptor lifetime token을 사용한다. Manual
descriptor에는 runtime이 CSPRNG로 만든 nonce를 사용하고 current connection handover fence와 함께 검증한다.
Application이 값을 선택하는 option은 없다. 순서를 비교하는 값은 `descriptorRevision`뿐이다. 이 revision이
`9223372036854775807n`인 상태에서 다음 값이 필요하면 host를 `Error`로 seal하고 wrap하지 않는다. Runtime은
lifetime token의 source를 application callback에 노출하지 않는다.

Entry·User·Instance Spot owner state는 global `SpotId`에서 파생한 하나의 authority key를 공유한다.
User Spot create와 Instance cold claim은 같은 row에 generic `reserve`를 수행하고 `commit` 또는 `abort`로
끝내므로 kind conflict, object generation과 target capacity가 원자적으로 결정된다. Public authority
compare-exchange로 Missing row를 만들지 않는다. `ZLinkSpotLocation`은 Framework가 authority
payload와 page를 decode한 운영 projection이며 provider write·remove·resolve interface가 아니다. Provider는
kind·stable type·descriptor allocation을 provider metadata로 처리하지만 application state와 Actor relocation
payload는 해석하지 않는다.
`SpotRef.objectGeneration`과 `ActorRef.objectGeneration`은 provider가 반환한
`objectGeneration`을 그대로 사용한다. Authority `authorityOwnerGeneration`은 per-object owner 이관 fence이고
descriptor·projection의 `leaseGeneration`은 host lease fence다. 두 generation을 합치거나 Framework 계산값으로
만들지 않는다.
Maintenance owner 이관은 `"newOwner"`로 owner generation만 바꾸고 object generation을 유지한다.
기존 ref의 object generation은 유지되며 이전 owner route를 사용하면 runtime이 current authority를 재조회하여 forwarding
또는 retry한다. Explicit close 뒤 cold recreate는 이전 row의 fenced delete가 완료된 후 새 `reserve`가 새 object
generation을 발급한다. 이전 handle은 영구적으로 stale다.

## 3. Owner와 relocation authority

```ts
declare const zlinkAuthorityKeyBrand: unique symbol;
declare const zlinkAuthorityVersionBrand: unique symbol;

export interface ZLinkAuthorityKey {
    readonly value: string;
    readonly [zlinkAuthorityKeyBrand]: true;
}

export interface ZLinkAuthorityStoreVersion {
    readonly value: string;
    readonly [zlinkAuthorityVersionBrand]: true;
}

export type ZLinkPlacementAllocationState = "reserved" | "active";

export interface ZLinkSpotTypeCapacityDelta {
    readonly objectKind: "user_spot" | "instance_spot";
    readonly stableType: string;
    readonly count: number;
}

export interface ZLinkCapacityVector {
    readonly actors: number;
    readonly spots: number;
    readonly spotType?: ZLinkSpotTypeCapacityDelta;
}

export interface ZLinkPlacementAllocation {
    readonly state: ZLinkPlacementAllocationState;
    readonly objectKind: ZLinkPlacementObjectKind;
    readonly stableType: string;
    readonly descriptor: ZLinkMeshNodeDescriptorKey;
    readonly descriptorLifecycleGeneration: bigint;
    readonly capacity: ZLinkCapacityVector;
}

export interface ZLinkReservedObjectCreation {
    readonly reservationId: string;
    readonly requestContentReference: string;
    readonly requestSha256: Uint8Array;
    readonly requestEncodedSize: bigint;
}

export type ZLinkAuthorityReadResult =
    | {
        readonly kind: "missing";
        readonly storeNow: Date;
    }
    | {
        readonly kind: "snapshot";
        readonly storeVersion: ZLinkAuthorityStoreVersion;
        readonly payload: Uint8Array;
        readonly objectGeneration: bigint;
        readonly authorityOwnerGeneration: bigint;
        readonly ownerId: string;
        readonly ownerLeaseGeneration: bigint;
        readonly allocation: ZLinkPlacementAllocation;
        readonly reservedCreation?: ZLinkReservedObjectCreation;
        readonly storeNow: Date;
    };

export type ZLinkAuthoritySnapshot = Extract<
    ZLinkAuthorityReadResult,
    { readonly kind: "snapshot" }>;

declare const zlinkRelocationCapacityFenceBrand: unique symbol;

export interface ZLinkRelocationCapacityFence {
    readonly value: string;
    readonly [zlinkRelocationCapacityFenceBrand]: true;
}

export type ZLinkAuthorityMutation =
    | {
        readonly kind: "put";
        readonly payload: Uint8Array;
        readonly generationTransition:
            "preserve" | "newOwner";
        readonly targetOwner?: ZLinkLocationOwnerToken;
        readonly relocationCapacityFence?: ZLinkRelocationCapacityFence;
    }
    | { readonly kind: "delete" };

export type ZLinkAuthorityCompareExchangeResult =
    | {
        readonly kind: "stored";
        readonly storeVersion: ZLinkAuthorityStoreVersion;
        readonly payload: Uint8Array;
        readonly objectGeneration: bigint;
        readonly authorityOwnerGeneration: bigint;
        readonly ownerId: string;
        readonly ownerLeaseGeneration: bigint;
        readonly allocation: ZLinkPlacementAllocation;
        readonly storeNow: Date;
    }
    | {
        readonly kind: "deleted";
        readonly storeVersion: ZLinkAuthorityStoreVersion;
        readonly storeNow: Date;
    }
    | {
        readonly kind: "conflict";
        readonly current: ZLinkAuthorityReadResult;
    }
    | { readonly kind: "generationExhausted" };

export interface ZLinkAuthorityStore {
    readAuthority(key: ZLinkAuthorityKey,
        signal?: AbortSignal): Promise<ZLinkAuthorityReadResult>;
    compareExchangeAuthority(key: ZLinkAuthorityKey,
        expectedStoreVersion: ZLinkAuthorityStoreVersion,
        mutation: ZLinkAuthorityMutation,
        signal?: AbortSignal): Promise<ZLinkAuthorityCompareExchangeResult>;
    listAuthorities(prefix: string, cursor: ZLinkAuthorityScanCursor | undefined,
        limit: number,
        signal?: AbortSignal): Promise<ZLinkAuthorityScanResult>;
}

export interface ZLinkAuthorityEntry {
    readonly key: ZLinkAuthorityKey;
    readonly snapshot: Extract<ZLinkAuthorityReadResult, { readonly kind: "snapshot" }>;
}

export declare class ZLinkAuthorityScanCursor {
    private constructor();
    static from(encoded: string): ZLinkAuthorityScanCursor;
    readonly encoded: string;
}

export interface ZLinkAuthorityPage {
    readonly kind: "page";
    readonly items: readonly ZLinkAuthorityEntry[];
    readonly nextCursor?: ZLinkAuthorityScanCursor;
}

export type ZLinkAuthorityScanResult =
    | ZLinkAuthorityPage
    | { readonly kind: "scanExpired" };
```

Authority key와 Store version은 Framework와 provider가 만든 opaque 값이다. `missing`은 `storeNow`만
반환하고 fake StoreVersion을 갖지 않는다. `compareExchangeAuthority`는 Active `"snapshot"`의 exact
StoreVersion을 받는 overload만 제공한다. `"preserve"`·`"newOwner"`·delete는 current StoreVersion을
`"found"`를 요구하며 `"missing"`과 Reserved allocation row에는 적용할 수 없다. Put의 `targetOwner`는 `"preserve"`에서
없어야 하고 `"newOwner"`에서 반드시 있어야 한다. Provider는 exact target owner lease를 CAS와 같은
transaction에서 검증하고 성공 결과의 `ownerId`·`ownerLeaseGeneration`으로 기록한다. 정상 create는 generic
reservation만 사용한다. `"preserve"`와 delete는 stored current owner lease, `"newOwner"`는 `targetOwner`
lease를 검증한다. Missing·stale lease는 current authority read를 가진 `"conflict"`로 끝나고 mutation은 0이다.
Invalid `targetOwner` 조합은 provider 호출 전에 `TypeError`로 거부한다. `relocationCapacityFence`는
`"newOwner"`에서 반드시 있고 `"preserve"`에서 없어야 한다. `"newOwner"` 성공은 fence의 source active
감소와 target reserved-to-active, target Active allocation과 authority owner metadata를 같은 transaction에서
적용한다. Provider는 allocation의 object kind·stable type을 capability·capacity metadata로 처리하지만
application·relocation payload는 해석하지 않는다. Framework runtime만 payload를 encode하고 phase 전이를 검증한다.

Provider domain은 영구적인 global object generation, authority owner generation과 Store revision counter를
각각 하나씩 유지한다. Initial object·owner generation은 `reserve`만 발급하고 `"newOwner"`는 owner
generation만 증가시키며 `"preserve"`는 둘 다 유지한다. Stored mutation과 delete는 global Store revision으로
fence한다. `reserve`는 Missing→Reserved, exact `commit`은 Reserved→Active, exact `abort`는
Reserved→Missing만 수행한다. `"preserve"`·`"newOwner"`·delete는 Active에만 적용한다. Delete는 current
Active allocation의 typed capacity vector를 감소시킨 뒤 row를 완전히 제거하고 per-key counter나 version tombstone을
유지하지 않는다. Scan lease가 활성화된 동안만 snapshot 유지용 tombstone을 bounded로 유지할 수 있다.
Authority payload에 generation과 current allocation을 중복 encode하지 않는다.
Authority row는 TTL을 갖지 않고 explicit fenced delete가 성공할 때까지 유지된다.
Owner·coordinator lease는 별도 token row에 저장하며 lease 만료나 reclaim이 authority row를 삭제하거나
수정하지 않는다.

세 global counter 중 하나가 `9223372036854775807n`인 상태에서 새 Store version·object generation·authority
owner generation이 필요한 CAS는 `{ kind: "generationExhausted" }`를 반환한다. 이 결과는 non-retriable이며
row·index·counter를 바꾸거나 값을 소비하지 않는다. 외부 상태가 바뀌지 않은 채 같은 key와 expectation으로
다시 호출하면 같은 결과를 반환한다. Provider·transport exception과는 서로 다른 결과다. Framework는 이를 기존 high-level lifecycle 실패로 종료하며
application public error enum을 추가하지 않는다.

Framework가 provider operation에 넘긴 `Uint8Array`는 반환된 Promise가 settle될 때까지 유효하고 내용이
바뀌지 않는다. Provider가 settle 뒤에도 payload를 보관하려면 그 전에 복사해야 한다. 성공 결과로 반환한
`Uint8Array` storage와 `Date` 객체는 Framework가 처리하는 동안 안정적이어야 하며 provider가 내용을 바꾸거나
재사용하지 않는다. Mutable-buffer adapter는 public boundary에서 snapshot을 만들고 runtime은 반환된 `Date`의
timestamp scalar를 즉시 snapshot한다. 호출 전에 `AbortSignal`이 이미 취소됐으면 provider를 호출하지 않고
I/O와 commit을 수행하지 않는다. Operation이 시작된 뒤 waiter cancellation이나 error가 발생하면 commit
여부는 unknown이며 no-commit을 보장하지 않는다. Authority CAS는 같은 key와 expected StoreVersion을 exact
read해 reconcile한 뒤 필요하면 retry한다. Content-addressed relocation put은 같은 content를 read·verify한 뒤
retry한다. Authority에 연결되지 않은 committed put은 orphan으로 retention까지 유지한 뒤 cleanup한다. 이
동작을 위한 public result는 추가하지 않는다.

Framework는 host process lifecycle마다 새 owner ID를 만들고 application에 owner ID 설정·재사용 API를
노출하지 않는다. 한 host의 모든 MeshNode·ClientServer·fanout descriptor와 authority는
같은 host token을 참조하고 각 descriptor가 자신의 RID를 갖는다. Provider domain은 영구적인 global
lease generation counter를 유지하고 claim이 성공할 때마다 증가시켜 1부터
`9223372036854775807n`까지의 token을 발급한다. Expiry·release는 active row를 삭제하고 같은
owner ID의 다음 claim은 더 큰 global generation을 받는다. 지연된 renew·release는 `"stale"`로
거부한다. Target admission 직전에 `readOwnerLease(ownerId)`로 exact token을 다시 확인한다. Owner lease 전체
목록과 snapshot type은 public surface에 제공하지 않는다.

Global lease generation counter가 `9223372036854775807n`에 도달한 뒤 새 generation이 필요한 claim은
`{ kind: "generationExhausted" }`를 반환한다. 만료된 row를 takeover하는 claim도 같은 규칙을 적용한다.
이 결과는 provider exception이나 retriable conflict가 아니며 row·index·counter를 바꾸거나 값을 소비하지
않는다. 같은 expectation을 다시 호출하면 외부 상태가 바뀌지 않는 한 같은 결과를 반환한다. Renew와 release는
새 generation이 필요하지 않으므로 이 결과를 추가하지 않는다.

Descriptor와 peer enumeration은 `ZLinkPageRequest`와 `ZLinkLocationPage<T>`를 사용한다. Effective
`pageSize`는 `1..1000`이며 continuation token은 provider만 해석하는 opaque value다. Provider는 encoded page가
4 MiB에 먼저 도달하면 요청보다 적은 item과 다음 token을 반환하며 byte limit public option은 제공하지 않는다.
Framework reconciler는 scope change stamp를 읽고 모든 page를 조립한 뒤 stamp를 다시 읽는다. 두 stamp가 같을
때만 full snapshot을 적용하고 다르면 부분 결과를 버리고 first page부터 다시 읽는다. Page 조립과 retry는
Framework 내부 동작이다.

Authority scan의 first page는 `cursor=undefined`로 요청한다. Provider는 한 snapshot을 만들고 이어지는
page에 필요한 모든 상태를 하나의 `ZLinkAuthorityScanCursor`에 담는다. 다음 page는 직전 page의
`nextCursor` 객체를 해석하거나 다시 조립하지 않고 그대로 넘긴다. `from`은 UTF-8 encoded 크기
`1..4096` bytes를 검증하고 empty cursor를 거부하며 반환 객체는 만든 뒤 바뀌지 않는다. Provider는
snapshot에 포함된 key incarnation을 scan 전체에서 각각 한 번만 반환한다. Concurrent delete는
Framework의 exact read에서 missing으로 제거되고 snapshot 뒤의 create·recreate는 다음 scan에서
반환된다. Framework는 각 candidate를 exact read한 뒤 current StoreVersion으로 CAS한다. 등록한
MeshName scope의 initial scan이 완료되기 전에는 Serving을 게시하지 않고 이후 scan은 background로
반복한다.
Provider가 cursor가 가리키는 scan을 만료시켰으면 이어지는 page 요청은 `"scanExpired"`를 반환한다.
Framework는 부분 결과를 사용하지 않고 first page부터 새 scan을 시작한다.

한 authority opaque payload의 encoded 크기는 최대 1 MiB다. Scan `limit`은 `1..1000`이고 provider는 encoded
page 4 MiB에 먼저 도달하면 요청보다 적은 entry와 `nextCursor`를 반환한다. 이 byte limit을 바꾸는
public option은 없다. Hot authority row는 compact metadata와 replay cursor만 보관하며 complete terminal reply
bytes는 relocation stream에 저장한다.

## 4. Relocation Store

```ts
declare const zlinkRelocationReferenceBrand: unique symbol;

export interface ZLinkRelocationReference {
    readonly value: string;
    readonly [zlinkRelocationReferenceBrand]: true;
}

export interface ZLinkRelocationStored {
    readonly reference: ZLinkRelocationReference;
    readonly checksumCrc32c: number;
    readonly expiresAt: Date;
    readonly storeNow: Date;
}

export type ZLinkRelocationReadResult =
    | { readonly kind: "found"; readonly payload: Uint8Array }
    | { readonly kind: "missing" };

export type ZLinkRelocationRenewResult =
    | { readonly kind: "renewed"; readonly expiresAt: Date; readonly storeNow: Date }
    | { readonly kind: "missing" };
export type ZLinkRelocationDeleteResult = "deleted" | "missing";

export interface ZLinkRelocationStore {
    putRelocation(payload: Uint8Array, retentionMs: number,
        signal?: AbortSignal): Promise<ZLinkRelocationStored>;
    getRelocation(reference: ZLinkRelocationReference,
        signal?: AbortSignal): Promise<ZLinkRelocationReadResult>;
    renewRelocation(reference: ZLinkRelocationReference, retentionMs: number,
        signal?: AbortSignal): Promise<ZLinkRelocationRenewResult>;
    deleteRelocation(reference: ZLinkRelocationReference,
        signal?: AbortSignal): Promise<ZLinkRelocationDeleteResult>;
}
```

Framework는 put과 renew의 `retentionMs`에 고정된 24시간만 전달한다. 이 값은 application option으로 노출하지
않는다. Authority의 current relocation reference를 확인한 owner 또는 recovery coordinator만
`renewRelocation`를 호출하며, 존재하지 않는 reference는 `"missing"` 정상 결과다. `"renewed"`는 provider
clock의 새 `expiresAt`과 `storeNow`를 반환한다. Runtime은 local clock으로 provider expiry를 추측하지 않는다.
`checksumCrc32c`는 저장된 immutable root bytes의 CRC32C(Castagnoli)를 나타내는 `0..0xFFFF_FFFF` 정수다.
Runtime은 이 값과 Location authority에 publish할 checksum이 정확히 같은지 검증한다.
`getRelocation`의 `missing`은 닫힌 정상 결과다. `deleteRelocation`는 reference가 없으면 `"missing"`을
반환하며 반복 호출해도 오류가 아니다. Provider는 relocation envelope과 업무 state를 해석하지 않는다.

Framework는 logical relocation payload를 immutable 64 MiB chunk 최대 4096개와 root manifest로 내부에서 나누므로
logical state ceiling은 256 GiB다. `ZLinkRelocationStore`의 opaque put/get interface는 바꾸지 않으며 chunk
크기, 개수와 manifest를 설정하는 public option도 제공하지 않는다. Capture가 ceiling을 넘으면 seal을 되돌려
normal messaging을 다시 허용하고 Retire 결과를 `"blocked"`로 종료한다. 일반 message의 negotiated effective
bound는 relocation chunk 크기 때문에 줄이지 않는다.

Location Store는 phase, relocation reference와 checksum, bounded canonical participant set, participant mutation,
aggregate generation, membership·aggregate count와 inventory digest를 authority로 소유한다. Relocation manifest는
opaque state, accepted journal, full inventory와 replay payload lookup에만 사용하며 authority가 아니다.
Framework는 relocation payload를 먼저 저장하고 manifest digest가 Location Store의 canonical inventory digest와
일치하는지 확인한 뒤 Location Store CAS로 reference를 공개한다. Root를 교체할 때도 새 root 저장과 digest
검증을 먼저 수행하고 CAS가 성공한 뒤 이전 reference를 release한 다음 이전 payload를 삭제한다. 두 Store 사이
transaction은 요구하지 않는다. Relocation payload 사용을 끝낼 때는 Location Store에서 reference 사용 종료를
CAS한 뒤 Relocation Store에서 payload를 삭제한다. Authority가 참조하는 payload가 없거나 digest가 다르면 `RelocationDataLost`로
종료하며 이전 owner로 rollback하지 않는다. Restore와 accepted journal replay는 manifest digest와
`inventoryDigest`가 exact match인 경우에만 시작한다.

## 5. Location Store와 선택 capability

```ts
export interface ZLinkObjectCreationKey {
    readonly kind: ZLinkPlacementObjectKind;
    readonly globalId: string;
}

export interface ZLinkObjectCreationTarget {
    readonly meshName: string;
    readonly nodeRid: RoutingId;
    readonly nodeLifecycleGeneration: bigint;
    readonly owner: ZLinkLocationOwnerToken;
}

export interface ZLinkObjectCreationIntent {
    readonly stableType: string;
    readonly requestContentReference: string;
    readonly requestSha256: Uint8Array;
    readonly requestEncodedSize: bigint;
}

export interface ZLinkCreationOperationId {
    readonly high: bigint;
    readonly low: bigint;
}

export interface ZLinkCreationOperationIdentity {
    readonly sourceNodeRid: RoutingId;
    readonly sourceNodeGeneration: bigint;
    readonly operationId: ZLinkCreationOperationId;
}

export type ZLinkCreationTerminalState =
    | 'created'
    | 'rejected'
    | 'failed';

export interface ZLinkCreationTerminalPublication {
    readonly operation: ZLinkCreationOperationIdentity;
    readonly terminalEnvelope: Uint8Array;
    readonly terminalEnvelopeSha256: Uint8Array;
    readonly operationDeadline: Date;
}

export interface ZLinkCreationTerminalRecord {
    readonly state: ZLinkCreationTerminalState;
    readonly operation: ZLinkCreationOperationIdentity;
    readonly reservationId: string;
    readonly objectKind: ZLinkPlacementObjectKind;
    readonly terminalEnvelope: Uint8Array;
    readonly terminalEnvelopeSha256: Uint8Array;
    readonly expiresAt: Date;
    readonly storeNow: Date;
}

export type ZLinkCreationTerminalReadResult =
    | { readonly kind: 'missing'; readonly storeNow: Date }
    | { readonly kind: 'found'; readonly record: ZLinkCreationTerminalRecord };

export interface ZLinkObjectReserveRequest {
    readonly key: ZLinkObjectCreationKey;
    readonly intent: ZLinkObjectCreationIntent;
    readonly target: ZLinkObjectCreationTarget;
    readonly creatingPayload: Uint8Array;
    readonly capacity: ZLinkCapacityVector;
}

export type ZLinkObjectReserveResult =
    | { readonly kind: 'reserved'; readonly reservationId: string; readonly creating: ZLinkAuthoritySnapshot }
    | { readonly kind: 'alreadyExists'; readonly current: ZLinkAuthoritySnapshot }
    | { readonly kind: 'typeMismatch'; readonly current: ZLinkAuthoritySnapshot }
    | { readonly kind: 'placementCapacityExhausted' }
    | { readonly kind: 'conflict'; readonly current: ZLinkAuthorityReadResult }
    | { readonly kind: 'generationExhausted' };

export interface ZLinkObjectCommitRequest {
    readonly key: ZLinkObjectCreationKey;
    readonly reservationId: string;
    readonly expectedStoreVersion: string;
    readonly target: ZLinkObjectCreationTarget;
    readonly readyPayload: Uint8Array;
}

export type ZLinkObjectCommitResult =
    | { readonly kind: 'committed'; readonly ready: ZLinkAuthoritySnapshot }
    | { readonly kind: 'alreadyCommitted'; readonly ready: ZLinkAuthoritySnapshot }
    | { readonly kind: 'stale' }
    | { readonly kind: 'generationExhausted' };

export type ZLinkObjectCreationCompletion =
    | {
        readonly kind: 'created';
        readonly readyPayload: Uint8Array;
        readonly terminal: ZLinkCreationTerminalPublication;
    }
    | {
        readonly kind: 'rejected' | 'failed';
        readonly terminal: ZLinkCreationTerminalPublication;
    };

export interface ZLinkObjectCreationCompleteRequest {
    readonly key: ZLinkObjectCreationKey;
    readonly reservationId: string;
    readonly expectedStoreVersion: string;
    readonly target: ZLinkObjectCreationTarget;
    readonly completion: ZLinkObjectCreationCompletion;
}

export type ZLinkObjectCreationCompleteResult =
    | {
        readonly kind: 'created';
        readonly ready: ZLinkAuthoritySnapshot;
        readonly terminal: ZLinkCreationTerminalRecord;
    }
    | {
        readonly kind: 'rejected' | 'failed' | 'alreadyCompleted';
        readonly terminal: ZLinkCreationTerminalRecord;
    }
    | { readonly kind: 'stale' }
    | { readonly kind: 'generationExhausted' };

export interface ZLinkObjectAbortRequest {
    readonly key: ZLinkObjectCreationKey;
    readonly reservationId: string;
    readonly expectedStoreVersion: string;
    readonly target: ZLinkObjectCreationTarget;
}

export type ZLinkObjectAbortResult =
    | { readonly kind: 'aborted' }
    | { readonly kind: 'alreadyAborted' }
    | { readonly kind: 'stale' };

export interface ZLinkRelocationCapacityReservationRequest {
    readonly reservationId: string;
    readonly authorityKey: ZLinkAuthorityKey;
    readonly expectedStoreVersion: ZLinkAuthorityStoreVersion;
    readonly objectKind: ZLinkPlacementObjectKind;
    readonly stableType: string;
    readonly sourceDescriptor: ZLinkMeshNodeDescriptorKey;
    readonly sourceNodeLifecycleGeneration: bigint;
    readonly sourceOwner: ZLinkLocationOwnerToken;
    readonly targetDescriptor: ZLinkMeshNodeDescriptorKey;
    readonly targetNodeLifecycleGeneration: bigint;
    readonly targetOwner: ZLinkLocationOwnerToken;
    readonly capacity: ZLinkCapacityVector;
}

export type ZLinkRelocationCapacityReserveResult =
    | { readonly kind: 'reserved'; readonly fence: ZLinkRelocationCapacityFence }
    | { readonly kind: 'alreadyReserved'; readonly fence: ZLinkRelocationCapacityFence }
    | { readonly kind: 'conflict'; readonly current: ZLinkAuthorityReadResult }
    | { readonly kind: 'targetUnavailable' }
    | { readonly kind: 'placementCapacityExhausted' };

export type ZLinkRelocationCapacityAbortResult =
    | 'aborted'
    | 'alreadyAborted'
    | 'alreadyCommitted'
    | 'stale';

export interface ZLinkRelocationCapacityStore {
    reserveRelocationCapacity(request: ZLinkRelocationCapacityReservationRequest,
        signal?: AbortSignal): Promise<ZLinkRelocationCapacityReserveResult>;
    abortRelocationCapacity(fence: ZLinkRelocationCapacityFence,
        signal?: AbortSignal): Promise<ZLinkRelocationCapacityAbortResult>;
}

declare const zlinkAggregateIdBrand: unique symbol;

export interface ZLinkAggregateId {
    readonly value: string;
    readonly [zlinkAggregateIdBrand]: true;
}

export interface ZLinkAggregateParticipant {
    readonly authorityKey: ZLinkAuthorityKey;
    readonly expectedStoreVersion: ZLinkAuthorityStoreVersion;
    readonly ownerTransition: 'preserve' | 'newOwner';
    readonly authorityPayload: Uint8Array;
    readonly membershipMutation: Uint8Array;
}

export interface ZLinkAggregatePrepareRequest {
    readonly aggregateId: ZLinkAggregateId;
    readonly aggregateGeneration: bigint;
    readonly participants: readonly ZLinkAggregateParticipant[];
    readonly inventoryDigest: Uint8Array;
    readonly targetDescriptor: ZLinkMeshNodeDescriptorKey;
    readonly targetDescriptorLifecycleGeneration: bigint;
    readonly capacity: ZLinkCapacityVector;
    readonly targetOwner: ZLinkLocationOwnerToken;
}

export interface ZLinkAggregateFence {
    readonly aggregateId: ZLinkAggregateId;
    readonly aggregateGeneration: bigint;
}

export type ZLinkAggregatePrepareResult =
    | { readonly kind: 'prepared'; readonly fence: ZLinkAggregateFence }
    | { readonly kind: 'alreadyPrepared'; readonly fence: ZLinkAggregateFence }
    | { readonly kind: 'conflict' }
    | { readonly kind: 'stale' }
    | { readonly kind: 'generationExhausted' };

export type ZLinkAggregateCommitResult =
    | { readonly kind: 'committed' }
    | { readonly kind: 'alreadyCommitted' }
    | { readonly kind: 'stale' }
    | { readonly kind: 'generationExhausted' };

export type ZLinkAggregateAbortResult =
    | { readonly kind: 'aborted' }
    | { readonly kind: 'alreadyAborted' }
    | { readonly kind: 'stale' };

export interface ZLinkObjectCreationStore {
    readCreationTerminal(operation: ZLinkCreationOperationIdentity,
        signal?: AbortSignal): Promise<ZLinkCreationTerminalReadResult>;
    reserve(request: ZLinkObjectReserveRequest,
        signal?: AbortSignal): Promise<ZLinkObjectReserveResult>;
    commit(request: ZLinkObjectCommitRequest,
        signal?: AbortSignal): Promise<ZLinkObjectCommitResult>;
    completeCreation(request: ZLinkObjectCreationCompleteRequest,
        signal?: AbortSignal): Promise<ZLinkObjectCreationCompleteResult>;
    abort(request: ZLinkObjectAbortRequest,
        signal?: AbortSignal): Promise<ZLinkObjectAbortResult>;
    prepareAggregate(request: ZLinkAggregatePrepareRequest,
        signal?: AbortSignal): Promise<ZLinkAggregatePrepareResult>;
    commitAggregate(fence: ZLinkAggregateFence,
        signal?: AbortSignal): Promise<ZLinkAggregateCommitResult>;
    abortAggregate(fence: ZLinkAggregateFence,
        signal?: AbortSignal): Promise<ZLinkAggregateAbortResult>;
}

export interface ZLinkLocationStore extends
    ZLinkMeshNodeLocationStore,
    ZLinkOwnerLeaseStore,
    ZLinkAuthorityStore,
    ZLinkObjectCreationStore,
    ZLinkRelocationCapacityStore {
    removeAllByOwner(owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<bigint>;
}

export enum ZLinkLocationChangeScopeKind {
    MeshNode = "meshNode",
    ClientServer = "clientServer",
    Spot = "spot",
    Authority = "authority",
    OwnerLease = "ownerLease",
    FanoutPublisher = "fanoutPublisher"
}

export interface ZLinkLocationChangeStampScope {
    readonly kind: ZLinkLocationKind | ZLinkLocationChangeScopeKind;
    readonly meshName?: string;
    readonly channelName?: string;
}

export interface ZLinkLocationChangeStampStore {
    getChangeStamp(scope: ZLinkLocationChangeStampScope,
        signal?: AbortSignal): Promise<bigint>;
}
```

Creation intent의 stable type은 UTF-8 1..255 bytes다. Encoded creation request는 최대 1 MiB이고
`requestContentReference`, SHA-256 hash와
`requestEncodedSize`는 같은 immutable content를 가리켜야 한다. `creatingPayload`와 `readyPayload`는 Framework가
encode한 opaque authority state다. Provider는 이를 해석하거나 합성하지 않고 각각 Reserve와 Commit의 authority
revision에 그대로 기록한다. `reserve(...)`는 같은 identity의 Ready row를
`alreadyExists`, 다른 kind·stable type을 `typeMismatch`, target typed population limit 초과를
`placementCapacityExhausted`로 닫는다. `capacity`는 Actor 전체 delta, Spot 전체 delta와 optional
User·Instance Spot kind·stable type delta를 한 vector로 보존한다. Actor 생성은 `(1, 0, undefined)`, Spot
생성은 `(0, 1, exact Spot type 1)`이다. User Spot aggregate relocation은 member Actor 수와 Spot 하나,
exact User Spot type 하나를 같은 vector에 둔다. 각 count는 0 이상이고 vector 전체는 하나 이상의 slot을
요청해야 한다. 새 generation을 발급할 수 없으면 `generationExhausted`다.

Reserved snapshot은 `reservedCreation`을 반드시 가지며 Active snapshot에는 이 field가 없어야 한다. Projection은
provider-issued reservation ID와 Actor·User Spot·Instance Spot 생성 요청의 immutable content reference, exact
32-byte SHA-256, `0..1 MiB` encoded size를 반환한다. Target-owned Instance Spot의 cold activation content만
complete `instance-activation-recovery-v1` envelope이며, Actor와 User Spot의 manager create content에는 이
envelope를 사용하지 않는다. Framework는 snapshot의 version·generation·owner·allocation과 함께 exact Commit
또는 Abort fence를 복원한다.

Actor 생성 command의 `OperationId`는 Reserve에 포함하지 않는다. Source runtime은 Reserve 전에
`readCreationTerminal(...)`을 호출하고, 같은 Actor가 Creating이라서 기다린 뒤 authority가 바뀌었을 때도
같은 operation의 terminal을 다시 조회한다. 같은 source Node RID·lifecycle generation·`OperationId`의
재전송만 저장된 terminal을 사용한다. 서로 다른 operation은 이전 요청의 application rejection reply를
사용하지 않는다. 승자가 `Created`로 끝나면 다른 operation은 Ready authority를 다시 조회해 `Existing`으로
끝나고, 승자가 `Rejected` 또는 infrastructure abort로 Creating을 제거하면 다른 operation은 새 reservation을
경쟁한다.

`terminalEnvelope`는 command 20 header나 correlation을 포함하지 않는
`creation-operation-terminal-v1` semantic envelope다. Terminal result, failure code, Actor create 의미 tail과
optional application payload envelope만 저장한다. 같은 operation을 재전송하면 Framework가 현재 request의
correlation과 reply route로 command 20 reply를 새로 encode한다. Envelope는 최대 1,048,576 bytes이고
`terminalEnvelopeSha256`은 envelope bytes의 exact 32-byte SHA-256이다. Provider는 terminal을 원래 operation
deadline보다 5분 뒤의 absolute Store time까지 보존하며 caller가 retention을 변경할 수 없다.

`commit(...)`은 reservation ID와 expected Store version을 exact 비교하고 target descriptor lifecycle과 owner
lease를 다시 확인한다. 이 operation은 Actor 이외의 generic creation을 Ready로 바꾼다. Actor는
`completeCreation(...)`만 사용한다. `created` completion은 terminal publication을 Ready
authority·reserved-to-active capacity 변경과 같은 transaction에서 기록한다. `rejected`와 `failed`
completion은 각각 application rejection과 callback failure의 terminal publication, Creating authority 제거와
reserved capacity 해제를 같은 transaction에서 처리한다. Terminal
expiry가 이미 지났거나 envelope size·SHA가 잘못되면 authority·capacity·reservation을 변경하기 전에
거부한다. `abort(...)`는 infrastructure failure와 recovery cleanup 전용이며 terminal을 publish하지 않고,
current lifecycle·lease를 요구하지 않은 채 reservation에 고정한 이전 descriptor·capacity counter를 exact
fence로 정리한다. 같은 operation의 completion을 반복하면 `alreadyCompleted`, generic Commit과 Abort를
반복하면 각각 `alreadyCommitted`, `alreadyAborted`를 반환한다. 다른 reservation 또는 version은 `stale`이며, 새 Store revision을 발급할 수
없으면 `generationExhausted`다. 이 결과들은 provider exception과 구분되는 닫힌 결과이며 row·capacity를
중복 변경하지 않는다.

Existing object relocation은 creation reservation을 재사용하지 않는다. `reserveRelocationCapacity`는 non-empty
128-bit `reservationId`, current authority version, kind·stable type, source·target descriptor key·lifecycle
generation과 exact owner token을
검증하고 `capacity` typed vector 전체를 target reserved capacity로 예약한다. Request source owner와
kind·stable type·descriptor key·lifecycle generation·capacity vector는 current authority owner와 durable
Active allocation에 정확히 일치해야 한다. Source descriptor row와 source owner lease의 live 상태는 요구하지
않는다. Target descriptor lifecycle·owner lease·capability와 typed population limit은 같은 transaction에서
live/exact로 검증한다. 같은 ID와 exact request는 `alreadyReserved`, 다른 내용은 `conflict`다. Standalone
Actor의 `"newOwner"` CAS만 relocation capacity fence를 소비하며 source active 감소와
target reserved-to-active를 authority mutation과 같은 transaction에서 처리한다. Commit 전
`abortRelocationCapacity`는 reserved vector를 해제한다. Reservation은 TTL로 만료시키지 않는다.
Standalone `"newOwner"` fence가 reserved 상태가 아니거나 authority key·expected Store version·source·target
owner와 일치하지 않으면 current authority read를 담은 `"conflict"`이며 authority·capacity·fence mutation은 0이다.
이미 committed·aborted된 fence도 같다. CAS transaction은 request source와 durable current Active
allocation의 exact match를 다시 확인하고 target descriptor lifecycle과
target owner lease만 live/exact로 재검증한다. Source descriptor row·lease가 stale·missing이어도 allocation
match가 유지되면 commit할 수 있다.

Aggregate ID는 zero가 아닌 128-bit 값이고 aggregate generation은 `1..9223372036854775807`이다. `participants`는
authority key의 canonical byte order로 정렬하며 중복이 없는 bounded canonical participant set이다. 한 prepare는
participant를 1..1024개 포함하며 participant payload와 membership mutation을 합친 encoded request가 1 MiB를
넘을 수 없다. `inventoryDigest`는 participant set과 mutation 전체를 canonical encode한 bytes의 32-byte SHA-256이다.
User Spot aggregate는 participant별 relocation capacity fence를 만들지 않는다. `capacity`는 Actor slot
`N`, Spot slot `1`, User Spot stable type slot `1`을 하나의 typed vector로 표현해야 한다.
`prepareAggregate(...)`는 모든 participant expectation과 durable Active allocation을 exact-match하고,
`targetDescriptor`, lifecycle generation과 `targetOwner`를 live/exact로 검증한 뒤 vector 전체를 같은
transaction에서 reserved capacity로 예약한다. Participant set 또는 vector가 맞지 않으면 `"conflict"`이며
authority, capacity와 aggregate record의 mutation은 0이다. 성공하면 durable aggregate state를
`Reserved`로 저장한다. Exact duplicate prepare만 `"alreadyPrepared"`다.
`commitAggregate(...)`는 aggregate record가 소유한 bundle만 소비해 모든 authority·membership·capacity
변경을 한 번에 공개한다. Commit 직전에 source Active allocation exact match와 target descriptor
lifecycle·owner lease를 다시 확인한다. Target이 stale이면 mutation 없이 reserved aggregate record를 유지하며 source
descriptor row·lease가 stale·missing이어도 allocation match가 유지되면 commit할 수 있다. 일부 participant만
보이는 상태를 허용하지 않는다. `abortAggregate(...)`는 준비된
변경을 폐기하고 aggregate record가 소유한 target reserved vector를 해제해 aborted로 닫는다. 같은 fence의 prepare·commit·abort 반복은 각각 `alreadyPrepared`, `alreadyCommitted`,
`alreadyAborted`로 끝나며 stale fence는 다른 aggregate generation을 변경하지 않는다.

`ZLinkLocationStore`는 MeshNode, owner lease와 generic authority CAS를 하나의 등록 단위로
묶는다. ClientServer, fanout과 change stamp는 이 객체가 추가로 구현할 수 있는 선택 capability다.
Relocation Store는 별도 등록한다. `Recreate` 또는 `Snapshot` factory가 하나라도 있거나 Instance Spot factory가
하나라도 있는 host는 정확히 하나 등록한다. Instance Spot factory가 없고 모든 factory가 `Disabled`인 same-node
구성만 이를 생략할 수 있다.

Spot과 Actor direct resolve는 Framework가 global key의 canonical authority를 읽고 opaque payload를 decode한다.
Operational Spot·Actor 목록은 `listAuthorities` 결과를 decode한 projection이며 routing authority로 사용하지
않는다.

Change stamp는 목록 조회를 줄이는 최적화이며 correctness authority가 아니다. Stamp가 없거나 유실돼도
polling과 authority read를 통해 같은 상태로 수렴해야 한다.

## 6. 공식 Redis package

```ts
export type RedisCommandValue = string | Buffer | number;

export interface RedisCommandClient {
    isOpen?: boolean;
    connect(): Promise<unknown>;
    sendCommand(args: RedisCommandValue[]): Promise<unknown>;
    quit?(): Promise<unknown>;
    disconnect?(): Promise<unknown>;
    on?(event: "error", listener: (error: unknown) => void): unknown;
}

export interface ZLinkRedisLocationOptions {
    readonly url?: string;
    readonly client?: RedisCommandClient;
    readonly clientOptions?: RedisClientOptions;
    readonly keyPrefix: string;
}

export interface ZLinkRedisRelocationOptions {
    readonly url?: string;
    readonly client?: RedisCommandClient;
    readonly clientOptions?: RedisClientOptions;
    readonly keyPrefix: string;
}

export declare class MutableZLinkRedisLocationOptions {
    url?: string;
    client?: RedisCommandClient;
    clientOptions?: RedisClientOptions;
    keyPrefix: string;
    setUrl(url: string): this;
    setClient(client: RedisCommandClient): this;
    setClientOptions(options: RedisClientOptions): this;
    setKeyPrefix(keyPrefix: string): this;
}

export declare class MutableZLinkRedisRelocationOptions {
    url?: string;
    client?: RedisCommandClient;
    clientOptions?: RedisClientOptions;
    keyPrefix: string;
    setUrl(url: string): this;
    setClient(client: RedisCommandClient): this;
    setClientOptions(options: RedisClientOptions): this;
    setKeyPrefix(keyPrefix: string): this;
}

export declare function configureOptions(
    configure: (options: MutableZLinkRedisLocationOptions) => void): ZLinkRedisLocationOptions;

export declare function configureRelocationOptions(
    configure: (options: MutableZLinkRedisRelocationOptions) => void): ZLinkRedisRelocationOptions;

export declare class ZLinkRedisLocationStore implements
    ZLinkLocationStore,
    ZLinkClientServerLocationStore,
    ZLinkFanoutLocationStore,
    ZLinkObjectCreationStore,
    ZLinkLocationChangeStampStore {
    constructor(options: ZLinkRedisLocationOptions |
        ((options: MutableZLinkRedisLocationOptions) => void));
    updateMeshNode(descriptor: ZLinkMeshNodeDescriptor, intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeMeshNode(key: ZLinkMeshNodeDescriptorKey, owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    listMeshNodes(meshName: string, page?: ZLinkPageRequest,
        signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkMeshNodeDescriptor>>;
    updatePeer(peer: ZLinkPeerLocation, intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removePeer(key: ZLinkPeerLocationKey, owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    listPeers(filter: ZLinkPeerLocationFilter, page?: ZLinkPageRequest,
        signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkPeerLocation>>;
    updateRoute(route: ZLinkRouteLocation, intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeRoute(key: ZLinkRouteLocationKey, owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    resolveRoute(key: ZLinkRouteLocationKey,
        signal?: AbortSignal): Promise<ZLinkRouteLocation | undefined>;
    listRoutes(filter: ZLinkRouteLocationFilter, page?: ZLinkPageRequest,
        signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkRouteLocation>>;
    claimOwnerLease(ownerId: string, leaseTtlMs: number,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseClaimResult>;
    readOwnerLease(ownerId: string,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseReadResult>;
    renewOwnerLease(token: ZLinkLocationOwnerToken, leaseTtlMs: number,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseRenewResult>;
    releaseOwnerLease(token: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkOwnerLeaseReleaseResult>;
    removeAllByOwner(owner: ZLinkLocationOwnerToken, signal?: AbortSignal): Promise<bigint>;
    updateClientServer(descriptor: ZLinkClientServerServerDescriptor,
        intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeClientServer(key: ZLinkClientServerServerDescriptorKey,
        owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    listClientServers(channelName: string, page?: ZLinkPageRequest,
        signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkClientServerServerDescriptor>>;
    updateFanoutPublisher(descriptor: ZLinkFanoutPublisherDescriptor,
        intent: ZLinkLocationWriteIntent,
        signal?: AbortSignal): Promise<ZLinkLocationWriteResult>;
    removeFanoutPublisher(key: ZLinkFanoutPublisherDescriptorKey,
        owner: ZLinkLocationOwnerToken,
        signal?: AbortSignal): Promise<ZLinkLocationWriteStatus>;
    listFanoutPublishers(channelName: string, page?: ZLinkPageRequest,
        signal?: AbortSignal): Promise<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>>;
    readAuthority(key: ZLinkAuthorityKey,
        signal?: AbortSignal): Promise<ZLinkAuthorityReadResult>;
    compareExchangeAuthority(key: ZLinkAuthorityKey,
        expectedStoreVersion: ZLinkAuthorityStoreVersion,
        mutation: ZLinkAuthorityMutation,
        signal?: AbortSignal): Promise<ZLinkAuthorityCompareExchangeResult>;
    listAuthorities(prefix: string, cursor: ZLinkAuthorityScanCursor | undefined,
        limit: number,
        signal?: AbortSignal): Promise<ZLinkAuthorityScanResult>;
    getChangeStamp(scope: ZLinkLocationChangeStampScope,
        signal?: AbortSignal): Promise<bigint>;
    readCreationTerminal(operation: ZLinkCreationOperationIdentity,
        signal?: AbortSignal): Promise<ZLinkCreationTerminalReadResult>;
    reserve(request: ZLinkObjectReserveRequest,
        signal?: AbortSignal): Promise<ZLinkObjectReserveResult>;
    commit(request: ZLinkObjectCommitRequest,
        signal?: AbortSignal): Promise<ZLinkObjectCommitResult>;
    completeCreation(request: ZLinkObjectCreationCompleteRequest,
        signal?: AbortSignal): Promise<ZLinkObjectCreationCompleteResult>;
    abort(request: ZLinkObjectAbortRequest,
        signal?: AbortSignal): Promise<ZLinkObjectAbortResult>;
    reserveRelocationCapacity(request: ZLinkRelocationCapacityReservationRequest,
        signal?: AbortSignal): Promise<ZLinkRelocationCapacityReserveResult>;
    abortRelocationCapacity(fence: ZLinkRelocationCapacityFence,
        signal?: AbortSignal): Promise<ZLinkRelocationCapacityAbortResult>;
    prepareAggregate(request: ZLinkAggregatePrepareRequest,
        signal?: AbortSignal): Promise<ZLinkAggregatePrepareResult>;
    commitAggregate(fence: ZLinkAggregateFence,
        signal?: AbortSignal): Promise<ZLinkAggregateCommitResult>;
    abortAggregate(fence: ZLinkAggregateFence,
        signal?: AbortSignal): Promise<ZLinkAggregateAbortResult>;
    close(): Promise<void>;
    dispose(): Promise<void>;
}

export declare class ZLinkRedisRelocationStore implements ZLinkRelocationStore {
    constructor(options: ZLinkRedisRelocationOptions |
        ((options: MutableZLinkRedisRelocationOptions) => void));
    putRelocation(payload: Uint8Array, retentionMs: number,
        signal?: AbortSignal): Promise<ZLinkRelocationStored>;
    getRelocation(reference: ZLinkRelocationReference,
        signal?: AbortSignal): Promise<ZLinkRelocationReadResult>;
    renewRelocation(reference: ZLinkRelocationReference, retentionMs: number,
        signal?: AbortSignal): Promise<ZLinkRelocationRenewResult>;
    deleteRelocation(reference: ZLinkRelocationReference,
        signal?: AbortSignal): Promise<ZLinkRelocationDeleteResult>;
    close(): Promise<void>;
    dispose(): Promise<void>;
}
```

위 타입은 `@zlink-systems/framework-locations-redis`가 export한다. 각 Store caller는 `url`, 외부에서 관리하는
`client`, 또는 `clientOptions` 중 하나와 비어 있지 않은 고유 `keyPrefix`를 별도로 지정한다. 두 Store는 같은
Redis deployment를 사용할 수 있지만 options, prefix와 lifecycle을 공유하지 않는다. Composite class는 제공하지
않으며 cross-store transaction도 요구하지 않는다. 각 class의 `close()`와 `dispose()`는 해당 Store의 종료를
요청하며, 종료가 시작된 뒤의 새 operation은 closed-store 오류로 실패한다.
Descriptor kind와 key namespace, authority CAS와 Relocation Store의 저장 규칙은
[Redis Location Store](../../../41-location-store-redis.ko.md)가 소유한다.
`ZLinkRedisLocationStore`는 같은 문서의 `location-authority-hybrid-v3`, schema epoch `3`,
creation·standalone relocation·aggregate HASH field set과 typed `capacityBundle` encoding을 그대로 사용한다.
JavaScript object property 이름, insertion order 또는 JSON serializer 결과를 durable schema로 사용하지 않는다.
공통 fixture의 unordered field-to-bytes map을 raw Redis write/read 양방향으로 검증해야 한다.
