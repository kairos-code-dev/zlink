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
factory가 하나라도 있으면 opaque state, accepted journal, full inventory와 replay payload를 보존하는 Relocation
Store도 정확히 하나 등록한다. `Disabled` factory만 있는 same-node 구성에는 Relocation Store가 필요하지 않다.
두 Store는 별도 객체와 별도 registration이다. 필요한 Store가 없거나 같은 capability가 중복 등록되면
Framework는 socket bind 전에 구성 오류로 종료한다.

완료 가능한 모든 cross-node Actor·Spot 이동은 Relocation Store를 사용한다. `Recreate`도 accepted journal과
recovery payload를 저장하며 `Snapshot`은 application state를 추가로 저장한다. Same-node Actor join은 Relocation
payload를 만들지 않고, `Disabled` cross-node 이동은 capture 전에 거부한다.

앞의 여섯 lease·polling duration은 모두 양수여야 하고 route cache와 relocation forwarding window는 0 이상이다.
기본값은 선언 순서대로 5000, 15000, 1000, 30000, 5000, 3000, 15000, 30000밀리초다.
`routeCacheMaxAgeMs`와 `relocationForwardingWindowMs`가 모두 양수이면 cache age가 forwarding window보다 최소
5000밀리초 작아야 한다. `ownerLeaseRenewIntervalMs`의 첫 번째 값은 Store owner lease 갱신
주기이며 service connection의 liveness interval이 아니다.
Relocation 제한의 기본값은 active outbound 64, active inbound 64, concurrent Capture 8, concurrent Restore 8,
encoded payload in flight 268,435,456 bytes다. 다섯 값은 모두 양수여야 하며 같은 process의 모든 MeshNode가
공유한다. Framework는 active unit, callback과 byte permit을 모두 얻기 전에는 source queue를 seal하지 않는다.
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
    readonly placementProfiles: readonly ZLinkPlacementProfile[];
    readonly activeLimit?: number;
    readonly pendingLimit?: number;
}

export interface ZLinkMeshNodeDescriptor {
    readonly meshName: string;
    readonly rid: RoutingId;
    readonly lifecycleGeneration: bigint;
    readonly descriptorRevision: bigint;
    readonly endpoint: string;
    readonly objectRole: ZLinkObjectRole;
    readonly placementWeight: number;
    readonly objectCapacity: {
        readonly activeObjects: number;
        readonly pendingActivations: number;
        readonly maxActiveObjects: number;
        readonly maxPendingActivations: number;
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
    readonly spotRid: RoutingId;
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
    readonly spotRid: SpotRid;
}

export interface ZLinkActorLocation {
    readonly meshName: string;
    readonly actorId: string;
    readonly actorType: string;
    readonly actorRef: ActorRef;
    readonly ownerNodeRid: RoutingId;
    readonly ownerNodeGeneration: bigint;
    readonly spotKind: ZLinkSpotKind;
    readonly spotRid: RoutingId;
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

`applicationVersion`은 `0..9223372036854775807` 범위의 `bigint`다. `objectCapabilities`는 Actor와 User·Instance
Spot을 object kind와 stable type별로 구분하고 policy, Snapshot adapter 등록 여부, placement profile과 type별
capacity limit을 같은 항목에 둔다. `hasSnapshotAdapter`는 해당 kind adapter의 등록 여부만 나타내며 state format,
version이나 contract ID를 싣지 않는다. Snapshot policy이면 이 값은 `true`여야 하고 Disabled·Recreate이면
`false`다. Current active·pending count는 `objectCapacity`에 둔다. Runtime state,
current capacity, maintenance wave 또는 weight가 바뀌면 descriptor
revision을 증가시킨다. ClientServer와 fanout은 RouteMesh descriptor의 key나 store operation을 재사용하지
않는다.

Descriptor의 key, RID, lifecycle generation, endpoint, security identity, owner token, application version,
ChannelName key set, Spot type set와 object capability의 kind·stable type·policy·Snapshot adapter 등록 여부·
placement profile·limit은 첫 admission 뒤
해당 lifecycle에서 바뀌지 않는다. Channel weight 값, current `objectCapacity`, maintenance wave와 runtime state만
mutable하다. Mutable update는 current owner token과 같은 lifecycle generation을 제시하고
`descriptorRevision`을 strictly 증가시켜야 한다. Provider는 stale revision이나 immutable field 변경을 원자적으로
거부하며 일부 field만 적용하지 않는다. ClientServer와 fanout descriptor도 같은 identity·revision fence를
적용한다.

`lifecycleGeneration`은 0이 아닌 opaque equality token이다. Runtime은 수치 크기로 lifecycle의 선후를
판정하지 않는다. Store-backed descriptor에는 exact owner lease·descriptor lifetime token을 사용한다. Manual
descriptor에는 runtime이 CSPRNG로 만든 nonce를 사용하고 current connection handover fence와 함께 검증한다.
Application이 값을 선택하는 option은 없다. 순서를 비교하는 값은 `descriptorRevision`뿐이다. 이 revision이
`9223372036854775807n`인 상태에서 다음 값이 필요하면 host를 `Error`로 seal하고 wrap하지 않는다. Runtime은
lifetime token의 source를 application callback에 노출하지 않는다.

Entry·User·Instance Spot owner state는 global `SpotRid`에서 파생한 하나의 authority key를 공유한다.
User Spot create와 Instance cold claim은 같은 row에 `"newObject"` compare-exchange를 수행하므로 kind
conflict와 object generation 증가가 원자적으로 결정된다. `ZLinkSpotLocation`은 Framework가 authority
payload와 page를 decode한 운영 projection이며 provider write·remove·resolve interface가 아니다. Provider는
Spot kind, type, owner state와 Actor relocation phase를 해석하지 않는다.
`SpotRef.objectGeneration`과 `ActorRef.objectGeneration`은 provider가 반환한
`objectGeneration`을 그대로 사용한다. Authority `authorityOwnerGeneration`은 per-object owner 이관 fence이고
descriptor·projection의 `leaseGeneration`은 host lease fence다. 두 generation을 합치거나 Framework 계산값으로
만들지 않는다.
Maintenance owner 이관은 `"newOwner"`로 owner generation만 바꾸고 object generation을 유지한다.
기존 ref의 object generation은 유지되며 이전 owner route를 사용하면 runtime이 current authority를 재조회하여 forwarding
또는 retry한다. Explicit close 후 cold recreate만 `"newObject"`로 새 object generation을 발급하며,
이때 이전 handle은 영구적으로 stale다.

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
        readonly storeNow: Date;
    };

export type ZLinkAuthoritySnapshot = Extract<
    ZLinkAuthorityReadResult,
    { readonly kind: "snapshot" }>;

export type ZLinkAuthorityMutation =
    | {
        readonly kind: "put";
        readonly payload: Uint8Array;
        readonly generationTransition:
            "preserve" | "newOwner" | "newObject";
    }
    | { readonly kind: "delete" };

export type ZLinkAuthorityExpectation =
    | { readonly kind: "missing" }
    | {
        readonly kind: "found";
        readonly storeVersion: ZLinkAuthorityStoreVersion;
    };

export type ZLinkAuthorityCompareExchangeResult =
    | {
        readonly kind: "stored";
        readonly storeVersion: ZLinkAuthorityStoreVersion;
        readonly payload: Uint8Array;
        readonly objectGeneration: bigint;
        readonly authorityOwnerGeneration: bigint;
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
        expectation: ZLinkAuthorityExpectation,
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
반환하고 fake StoreVersion을 갖지 않는다. `compareExchangeAuthority`는 `ZLinkAuthorityExpectation`을
받는 overload만 제공한다. `"newObject"`는 `"missing"`을, `"preserve"`·`"newOwner"`·delete는 current
StoreVersion을 담은 `"found"`를 요구한다. Provider는 payload,
object kind, relocation phase와 relocation reference를 해석하지 않는다. Framework runtime만 payload를 encode하고
phase 전이를 검증한다.

Provider domain은 영구적인 global object generation, authority owner generation과 Store revision counter를
각각 하나씩 유지한다. CAS 성공 operation에서 `"newObject"`는 object와 owner generation을 모두
증가시키고, `"newOwner"`는 owner generation만 증가시키며 `"preserve"`는 둘 다 유지한다. Stored
mutation과 delete는 global Store revision으로 fence한다. Delete는 row를 완전히 제거하고 per-key counter나
version tombstone을 유지하지 않는다. Scan lease가 활성화된 동안만 snapshot 유지용 tombstone을 bounded로
유지할 수 있다. Authority payload에 generation을 중복 encode하지 않는다.
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
    readonly placementProfile?: ZLinkPlacementProfile;
    readonly affinityKey?: ZLinkAffinityKey;
    readonly requestContentReference: string;
    readonly requestSha256: Uint8Array;
    readonly requestEncodedSize: bigint;
}

export interface ZLinkObjectReserveRequest {
    readonly key: ZLinkObjectCreationKey;
    readonly intent: ZLinkObjectCreationIntent;
    readonly target: ZLinkObjectCreationTarget;
    readonly pendingCapacityDelta: number;
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
}

export type ZLinkObjectCommitResult =
    | { readonly kind: 'committed'; readonly ready: ZLinkAuthoritySnapshot }
    | { readonly kind: 'alreadyCommitted'; readonly ready: ZLinkAuthoritySnapshot }
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
    readonly targetReservations: readonly ZLinkObjectCommitRequest[];
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
    reserve(request: ZLinkObjectReserveRequest,
        signal?: AbortSignal): Promise<ZLinkObjectReserveResult>;
    commit(request: ZLinkObjectCommitRequest,
        signal?: AbortSignal): Promise<ZLinkObjectCommitResult>;
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
    ZLinkObjectCreationStore {
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

Creation intent의 stable type은 UTF-8 1..255 bytes이며 `placementProfile`과 `affinityKey`도 각각 UTF-8
1..255 bytes다. Encoded creation request는 최대 1 MiB이고 `requestContentReference`, SHA-256 hash와
`requestEncodedSize`는 같은 immutable content를 가리켜야 한다. `reserve(...)`는 같은 identity의 Ready row를
`alreadyExists`, 다른 kind·stable type을 `typeMismatch`, target pending limit 초과를
`placementCapacityExhausted`로 닫는다. 새 generation을 발급할 수 없으면 `generationExhausted`다.

`commit(...)`과 `abort(...)`는 reservation ID와 expected Store version을 exact 비교한다. 같은 terminal
operation을 반복하면 각각 `alreadyCommitted`와 `alreadyAborted`를 반환한다. 다른 reservation 또는 version은
`stale`이며, commit에 새 generation이 필요하지만 발급할 수 없으면 `generationExhausted`다. 이 결과들은
provider exception과 구분되는 닫힌 결과이며 row·capacity를 중복 변경하지 않는다.

Aggregate ID는 zero가 아닌 128-bit 값이고 aggregate generation은 `1..9223372036854775807`이다. `participants`는
authority key의 canonical byte order로 정렬하며 중복이 없는 bounded canonical participant set이다. 한 prepare는
participant를 1..1024개 포함하며 participant payload와 membership mutation을 합친 encoded request가 1 MiB를
넘을 수 없다. `inventoryDigest`는 participant set과 mutation 전체를 canonical encode한 bytes의 32-byte SHA-256이다.
`prepareAggregate(...)`는 모든 participant의 Store version, owner transition과 target
reservation을 함께 검증한다. `commitAggregate(...)`는 모든 authority·membership·capacity 변경을 한 번에
공개하며 일부 participant만 보이는 상태를 허용하지 않는다. `abortAggregate(...)`는 준비된 변경을 전부
폐기한다. 같은 fence의 prepare·commit·abort 반복은 각각 `alreadyPrepared`, `alreadyCommitted`,
`alreadyAborted`로 끝나며 stale fence는 다른 aggregate generation을 변경하지 않는다.

`ZLinkLocationStore`는 MeshNode, owner lease와 generic authority CAS를 하나의 등록 단위로
묶는다. ClientServer, fanout과 change stamp는 이 객체가 추가로 구현할 수 있는 선택 capability다.
Relocation Store는 별도 등록하며 `Recreate` 또는 `Snapshot` factory가 있는 host에서 필수다.

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
        expectation: ZLinkAuthorityExpectation,
        mutation: ZLinkAuthorityMutation,
        signal?: AbortSignal): Promise<ZLinkAuthorityCompareExchangeResult>;
    listAuthorities(prefix: string, cursor: ZLinkAuthorityScanCursor | undefined,
        limit: number,
        signal?: AbortSignal): Promise<ZLinkAuthorityScanResult>;
    getChangeStamp(scope: ZLinkLocationChangeStampScope,
        signal?: AbortSignal): Promise<bigint>;
    reserve(request: ZLinkObjectReserveRequest,
        signal?: AbortSignal): Promise<ZLinkObjectReserveResult>;
    commit(request: ZLinkObjectCommitRequest,
        signal?: AbortSignal): Promise<ZLinkObjectCommitResult>;
    abort(request: ZLinkObjectAbortRequest,
        signal?: AbortSignal): Promise<ZLinkObjectAbortResult>;
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
