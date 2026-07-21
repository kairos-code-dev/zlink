# Kotlin Location과 maintenance 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java Location](../../java/interfaces/location-maintenance.ko.md)

Kotlin은 Java `ZLinkAuthorityStore`, `ZLinkCheckpointStore`, 닫힌 result와 cancellation type을 그대로 사용한다.
Checkpoint TTL 갱신도 Java `renew(reference, retention, cancellation)`을 직접 사용하며 별도 Kotlin wrapper나
application retention option을 만들지 않는다. Renew 성공은 Java `ZLinkCheckpointRenewed`의 provider
`expiresAt`과 `storeNow`를 반환한다.
Logical checkpoint의 immutable 64 MiB chunk, 최대 4096개, 256 GiB ceiling과 root manifest도 공유 JVM
runtime이 내부에서 적용한다. Kotlin public chunk option이나 store wrapper를 만들지 않는다. Ceiling 초과 시
seal rollback과 `BLOCKED` Retire 결과, 일반 message bound와 checkpoint chunk bound의 분리는 Java 계약과 같다.
Authority generation도 Java provider envelope를 그대로 사용한다. `PRESERVE`, `NEW_OWNER`, `NEW_OBJECT` transition과
provider가 발급한 object·owner generation을 Kotlin 별도 DTO나 Framework 계산값으로 복제하지 않는다.
세 global counter 중 하나가 `Long.MAX_VALUE`인 상태에서 새 Store revision·object generation·authority owner
generation이 필요한 CAS는 Java `ZLinkAuthorityGenerationExhausted`를 반환한다. Owner lease claim에서 새
generation을 만들 수 없으면 `ZLinkOwnerLeaseGenerationExhausted`를 반환한다. 만료된 row takeover도 같은
결과를 사용하며 renew·release에는 이를 추가하지 않는다. 두 결과는 non-retriable이고 row·index·counter를
바꾸거나 값을 소비하지 않는다. 외부 상태가 바뀌지 않은 같은 expectation은 같은 결과를 반환한다. Routing
slot의 group exhaustion과 provider exception은 별도 결과다.
Authority row는 TTL 없이 explicit fenced delete까지 유지되며 owner·coordinator lease는 별도 token row에
저장된다. Lease 만료나 reclaim은 authority row를 삭제하거나 수정하지 않는다.
Authority opaque payload는 최대 1 MiB이고 scan request는 `1..1000` item을 요청한다. First page는 empty
`Optional<ZLinkAuthorityScanCursor>`로 요청한다. Provider는 한 snapshot을 만들고 다음 page에 필요한 모든
상태를 하나의 immutable cursor에 담는다. Kotlin runtime은 직전 page가 반환한 cursor 객체를 해석하거나
다시 조립하지 않고 그대로 전달한다. Cursor의 UTF-8 encoded 크기는 `1..4096` bytes이며 empty cursor는
허용하지 않는다. Provider는 encoded page 4 MiB에 먼저 도달하면 더 적은 entry와 next cursor를 반환한다.
Cursor가 가리키는 scan이 만료되면 닫힌 `ZLinkAuthorityScanExpired` 결과를 반환하고 Framework는 부분 결과를
버린 뒤 first page부터 다시 읽는다. Hot row는 compact metadata와 replay cursor만 보관하고 complete terminal
reply bytes는 checkpoint stream에 저장한다. Kotlin byte limit option은 제공하지 않는다.
Store operation을 복제한 suspend interface를 만들지 않으며 application service가 authority CAS나 checkpoint
reference를 조립하도록 하는 DSL도 제공하지 않는다. Owner lease option은 Java `ZLinkLocationOptions`를 사용하고
application traffic과 무관한 5초 periodic probe·같은 current connection의 matching ACK 15초 deadline과
합치지 않는다. 다른 inbound frame은 ACK deadline을 충족하지 않는다.
Java `ownerLeaseFencingMargin` 명칭과
`ownerLeaseRenewInterval + ownerLeaseRenewTimeout < ownerLeaseTtl - ownerLeaseFencingMargin` 관계를 routing
allocation 여부와 관계없이 모든 Location owner lease host에 적용한다. Compatibility alias는 제공하지 않는다.
`storeFailureGrace`는 discovery reconcile과 새 outbound connect에만 적용해 마지막 stable desired set을 grace
동안 고정한다. Existing admitted transport에는 service liveness를 적용하고 grace 뒤 stable snapshot 전에는 새
connect를 금지한다. 이 값은 owner·coordinator lease와 local authority deadline을 연장하지 않으며 stateful
admission은 마지막 valid monotonic lease deadline에서 닫힌다. Recovery는 exact owner token과 stable page set을
재검증한 뒤 diff와 connect를 수행한다.
Framework가 Java provider에 넘긴 `byte[]`은 `CompletionStage`가 완료될 때까지 유효하고 내용이 바뀌지 않는다.
Provider가 완료 뒤 보관하려면 완료 전에 복사하고, 성공 결과로 반환한 storage를 Framework가 처리하는 동안
바꾸거나 재사용하지 않는다. Kotlin mutable buffer adapter는 public boundary에서 snapshot을 만든다. 시작 전
cancellation이면 provider 호출·I/O·commit이 없고, 시작 뒤 waiter cancellation이나 error이면 commit 여부는
unknown이다. Authority CAS는 같은 key와 expected StoreVersion을 exact read해 reconcile한 뒤 retry한다.
Content-addressed checkpoint put은 read·verify한 뒤 retry하며 authority에 연결되지 않은 committed put은
retention이 끝난 뒤 cleanup한다. 별도 Kotlin public result는 만들지 않는다.
Framework는 host process lifecycle마다 새 owner ID를 만들고 application에 owner ID 설정·재사용 API를
노출하지 않는다. 한 host의 모든 descriptor와 authority는 같은 host token을 참조하고 각
descriptor가 자신의 RID를 갖는다. Java provider domain은 영구적인 global lease generation counter를
유지하고 claim이 성공할 때마다 1부터 `Long.MAX_VALUE`까지의 token을 발급한다. Expiry·release는
active row를 삭제하고 같은 owner ID의 다음 claim은 더 큰 global generation을 받는다. 지연된
renew·release는 `STALE`로 거부한다.
Owner lease 전체 목록과 snapshot type은 제공하지 않으며 `readOwnerLease(ownerId)`만 exact admission에
사용한다. Descriptor와 peer enumeration은 Java `ZLinkPageRequest`와 `ZLinkLocationPage<T>`를 그대로
사용한다. `firstPage()`는 100개를 요청하고 명시한 page size는 `1..1000`이다. Provider는 encoded page 4 MiB에
먼저 도달하면 더 적은 item과 opaque continuation token을 반환한다. Framework는 scope change stamp 전후가
같을 때만 조립한 full snapshot을 적용하며 Kotlin public reconciliation API나 byte limit option을 만들지 않는다.
Routing ID slot acquire는 이미 claim한 같은 Java owner token을 받고 별도 TTL이나 token을
발급하지 않는다. Startup rollback은 slot을 먼저 release한 뒤 host lease를 마지막에 release한다. Java의
`slotCount`·slot `1..65535`, group member `1..255` 범위와 coherent unpaged group snapshot을 그대로 적용한다.

Generic peer·route extension은 application이 직접 구성한 discovery store를 조작한다. Stateful owner와 transfer는
Java `ZLinkLocationStore`가 함께 제공하는 opaque authority CAS capability를 사용한다. Kotlin package는
Actor·Instance phase별 Store나 application이 transfer phase를 조립하는 extension을 추가하지 않는다.

Store 없이 만든 Actor의 authority와 handle generation은 runtime-local opaque token이며 같은 process 안에서만
message를 처리한다. Remote directory·resolve, distributed join·session binding, transfer·relocation은 구성 또는
operation 오류로 거부한다. Durable authority와 owner generation은 Store-backed Actor에만 적용하며 이 제한을
완화하는 Kotlin option은 제공하지 않는다.

Descriptor의 immutable identity/configuration field와 mutable weight·capacity·maintenance wave·runtime state
구분, current owner token과 strictly increasing revision fence도 Java 계약을 그대로 사용한다. Stale revision이나
immutable field 변경은 원자적으로 거부하며 일부 field만 적용하지 않는다.
`lifecycleGeneration`은 0이 아닌 opaque equality token이며 수치로 순서를 비교하지 않는다. Store-backed
descriptor는 exact owner lease·descriptor lifetime token을 사용한다. Manual descriptor는 runtime이 CSPRNG로
만든 nonce를 current connection handover fence와 함께 검증하며 caller option은 없다. 순서가 있는 값은
`descriptorRevision`뿐이다. `Long.MAX_VALUE` 뒤의 다음 revision이 필요하면 host를 `ERROR`로 seal하고 wrap하지
않으며 token source를 application callback에 노출하지 않는다.

## Kotlin source signature

```kotlin
abstract class ZLinkSuspendingLocationStore(
    scope: CoroutineScope = dispatcherScope(Dispatchers.IO),
    dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ZLinkLocationStore,
    ZLinkClientServerLocationStore,
    ZLinkFanoutLocationStore,
    ZLinkPeerLocationStore,
    ZLinkRouteLocationStore {
    protected fun <T> async(block: suspend () -> T): CompletionStage<T>

    protected abstract suspend fun updateMeshNodeSuspending(
        descriptor: ZLinkMeshNodeDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removeMeshNodeSuspending(
        key: ZLinkMeshNodeDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteStatus
    protected abstract suspend fun listMeshNodesSuspending(
        meshName: String,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkMeshNodeDescriptor>

    protected abstract suspend fun updateClientServerSuspending(
        descriptor: ZLinkClientServerServerDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removeClientServerSuspending(
        key: ZLinkClientServerServerDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteStatus
    protected abstract suspend fun listClientServersSuspending(
        channelName: String,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkClientServerServerDescriptor>

    protected abstract suspend fun updateFanoutPublisherSuspending(
        descriptor: ZLinkFanoutPublisherDescriptor,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removeFanoutPublisherSuspending(
        key: ZLinkFanoutPublisherDescriptorKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteStatus
    protected abstract suspend fun listFanoutPublishersSuspending(
        channelName: String,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>

    protected abstract suspend fun updatePeerSuspending(
        peer: ZLinkPeerLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removePeerSuspending(
        key: ZLinkPeerLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun listPeerLocationsSuspending(
        filter: ZLinkPeerLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkPeerLocation>

    protected abstract suspend fun updateRouteSuspending(
        route: ZLinkRouteLocation,
        intent: ZLinkLocationWriteIntent,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun removeRouteSuspending(
        key: ZLinkRouteLocationKey,
        owner: ZLinkLocationOwnerToken,
    ): ZLinkLocationWriteResult
    protected abstract suspend fun resolveRouteSuspending(
        key: ZLinkRouteLocationKey,
    ): ZLinkRouteLocation?
    protected abstract suspend fun listRouteLocationsSuspending(
        filter: ZLinkRouteLocationFilter,
        page: ZLinkPageRequest,
    ): ZLinkLocationPage<ZLinkRouteLocation>

    protected abstract suspend fun claimOwnerLeaseSuspending(
        ownerId: String,
        leaseTtl: Duration,
    ): ZLinkOwnerLeaseClaimResult
    protected abstract suspend fun readOwnerLeaseSuspending(
        ownerId: String,
    ): ZLinkOwnerLeaseReadResult
    protected abstract suspend fun renewOwnerLeaseSuspending(
        token: ZLinkLocationOwnerToken,
        leaseTtl: Duration,
    ): ZLinkOwnerLeaseRenewResult
    protected abstract suspend fun releaseOwnerLeaseSuspending(
        token: ZLinkLocationOwnerToken,
    ): ZLinkOwnerLeaseReleaseResult
    protected abstract suspend fun removeAllByOwnerSuspending(
        owner: ZLinkLocationOwnerToken,
    ): Long
    protected abstract suspend fun readAuthoritySuspending(
        key: String,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAuthorityReadResult
    protected abstract suspend fun compareExchangeAuthoritySuspending(
        key: String,
        expectation: ZLinkAuthorityExpectation,
        mutation: ZLinkAuthorityMutation,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAuthorityWriteResult
    protected abstract suspend fun listAuthoritiesSuspending(
        prefix: String,
        cursor: Optional<ZLinkAuthorityScanCursor>,
        limit: Int,
        cancellation: ZLinkStoreCancellation,
    ): ZLinkAuthorityScanResult
}
```

위 base class의 Java override는 final이며 각 `protected` suspending member를 `CompletionStage`에 연결한다.
이 base class를 상속하면 ClientServer와 fanout 선택 capability도 함께 제공하므로 subclass는 해당 bridge를
모두 구현해야 한다. 두 capability를 제공하지 않는 store는 Java의 최소 `ZLinkLocationStore`를 직접 구현한다.
Spot provider write·remove·resolve bridge는 제공하지 않는다. Entry·User·Instance Spot은 Java authority
capability의 `(MeshName, SpotRid)` key와 object generation을 공유한다. Spot 운영 조회는 Framework가 authority
payload를 decode한 projection을 반환한다. Constructor의 기본 dispatcher는 convenience default이며 store
operation의 ordering이나 authority 의미를 바꾸지 않는다.
Spot projection과 Spot handle의 generation, `ActorRef.generation()`은 Java provider의 `objectGeneration`을
그대로 사용한다. Authority `authorityOwnerGeneration`은 per-object owner 이관 fence이고 descriptor·projection의
`leaseGeneration`은 host lease fence다. Target admission 직전에 exact owner lease token을 재조회한다.
Maintenance owner 이관은 Java `NEW_OWNER`를 사용하여 object generation을 유지한다. 기존 handle은
유효하며 stale owner route는 current authority를 재조회해 forwarding 또는 retry한다. Explicit close 후
cold recreate만 `NEW_OBJECT`로 새 object generation을 발급하며 이전 handle은 영구적으로 stale다.

```kotlin
suspend fun ZLinkMeshNodeLocationStore.updateMeshNode(
    descriptor: ZLinkMeshNodeDescriptor,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkMeshNodeLocationStore.removeMeshNode(
    key: ZLinkMeshNodeDescriptorKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteStatus
suspend fun ZLinkMeshNodeLocationStore.listMeshNodes(
    meshName: String,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkMeshNodeDescriptor>

suspend fun ZLinkClientServerLocationStore.updateClientServer(
    descriptor: ZLinkClientServerServerDescriptor,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkClientServerLocationStore.removeClientServer(
    key: ZLinkClientServerServerDescriptorKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteStatus
suspend fun ZLinkClientServerLocationStore.listClientServers(
    channelName: String,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkClientServerServerDescriptor>

suspend fun ZLinkFanoutLocationStore.updateFanoutPublisher(
    descriptor: ZLinkFanoutPublisherDescriptor,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkFanoutLocationStore.removeFanoutPublisher(
    key: ZLinkFanoutPublisherDescriptorKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteStatus
suspend fun ZLinkFanoutLocationStore.listFanoutPublishers(
    channelName: String,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>

suspend fun ZLinkPeerLocationStore.updatePeer(
    peer: ZLinkPeerLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkPeerLocationStore.removePeer(
    key: ZLinkPeerLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult
suspend fun ZLinkPeerLocationStore.listPeerLocations(
    filter: ZLinkPeerLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkPeerLocation>

suspend fun ZLinkRouteLocationStore.updateRoute(
    route: ZLinkRouteLocation,
    intent: ZLinkLocationWriteIntent,
): ZLinkLocationWriteResult
suspend fun ZLinkRouteLocationStore.removeRoute(
    key: ZLinkRouteLocationKey,
    owner: ZLinkLocationOwnerToken,
): ZLinkLocationWriteResult
suspend fun ZLinkRouteLocationStore.resolveRoute(
    key: ZLinkRouteLocationKey,
): ZLinkRouteLocation?
suspend fun ZLinkRouteLocationStore.listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation>

suspend fun ZLinkLocationStore.claimOwnerLease(
    ownerId: String,
    leaseTtl: Duration,
): ZLinkOwnerLeaseClaimResult
suspend fun ZLinkLocationStore.readOwnerLease(
    ownerId: String,
): ZLinkOwnerLeaseReadResult
suspend fun ZLinkLocationStore.renewOwnerLease(
    token: ZLinkLocationOwnerToken,
    leaseTtl: Duration,
): ZLinkOwnerLeaseRenewResult
suspend fun ZLinkLocationStore.releaseOwnerLease(
    token: ZLinkLocationOwnerToken,
): ZLinkOwnerLeaseReleaseResult
suspend fun ZLinkLocationStore.removeAllByOwner(owner: ZLinkLocationOwnerToken): Long

suspend fun ZLinkPeerLocationResolver.listLivePeers(
    filter: ZLinkPeerLocationFilter,
): List<ZLinkPeerLocation>
suspend fun SpotHandleResolver.resolveSpotHandle(
    meshName: String,
    spotRid: RoutingId,
): SpotHandle?
suspend fun ActorSpotHandleResolver.resolveActorSpotHandle(
    meshName: String,
    actorId: String,
): SpotHandle?

suspend fun ZLinkLocationRuntimeQuery.status(): ZLinkLocationRuntimeStatus
suspend fun ZLinkLocationRuntimeQuery.listPeerLocations(
    filter: ZLinkPeerLocationFilter,
): List<ZLinkPeerLocation>
suspend fun ZLinkLocationRuntimeQuery.listSpotLocations(
    filter: ZLinkSpotLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkSpotLocation>
suspend fun ZLinkLocationRuntimeQuery.listActorLocations(
    filter: ZLinkActorLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkActorLocation>
suspend fun ZLinkLocationRuntimeQuery.listRouteLocations(
    filter: ZLinkRouteLocationFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkRouteLocation>
suspend fun ZLinkLocationRuntimeQuery.listTopology(
    filter: ZLinkLocationTopologyFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkLocationTopologyEntry>
suspend fun ZLinkLocationRuntimeQuery.listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
): List<ZLinkLocationServiceSummary>

fun <T> locationPages(
    firstPage: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
    load: (ZLinkPageRequest) -> CompletionStage<ZLinkLocationPage<T>>,
): Flow<T>
fun ZLinkLocationRuntimeQuery.spots(
    filter: ZLinkSpotLocationFilter,
    pageSize: Int,
): Flow<ZLinkSpotLocation>
fun ZLinkLocationRuntimeQuery.actors(
    filter: ZLinkActorLocationFilter,
    pageSize: Int,
): Flow<ZLinkActorLocation>
fun ZLinkLocationRuntimeQuery.routes(
    filter: ZLinkRouteLocationFilter,
    pageSize: Int,
): Flow<ZLinkRouteLocation>
fun ZLinkLocationRuntimeQuery.topology(
    filter: ZLinkLocationTopologyFilter,
    pageSize: Int,
): Flow<ZLinkLocationTopologyEntry>
fun ZLinkLocationWatchStore.changes(
    filter: ZLinkLocationWatchFilter,
): Flow<ZLinkLocationChanged>
fun <T> Publisher<T>.asFlow(): Flow<T>
```

## Exact generated JVM signature

아래 JVM signature는 Kotlin source contract의 generated form이다.

```java
public final class systems.zlink.framework.kotlin.ZLinkLocationExtensionsKt {
  public static final <T> kotlinx.coroutines.flow.Flow<T> locationPages(systems.zlink.framework.locations.ZLinkPageRequest, kotlin.jvm.functions.Function1<? super systems.zlink.framework.locations.ZLinkPageRequest, ? extends java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<T>>>);
  public static kotlinx.coroutines.flow.Flow locationPages$default(systems.zlink.framework.locations.ZLinkPageRequest, kotlin.jvm.functions.Function1, int, java.lang.Object);
  public static final kotlinx.coroutines.flow.Flow<systems.zlink.framework.locations.ZLinkSpotLocation> spots(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkSpotLocationFilter, int);
  public static final kotlinx.coroutines.flow.Flow<systems.zlink.framework.locations.ZLinkActorLocation> actors(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkActorLocationFilter, int);
  public static final kotlinx.coroutines.flow.Flow<systems.zlink.framework.locations.ZLinkRouteLocation> routes(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkRouteLocationFilter, int);
  public static final kotlinx.coroutines.flow.Flow<systems.zlink.framework.locations.ZLinkLocationTopologyEntry> topology(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkLocationTopologyFilter, int);
  public static final java.lang.Object updateMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeLocationStore, systems.zlink.framework.locations.ZLinkMeshNodeDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  public static final java.lang.Object removeMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeLocationStore, systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteStatus>);
  public static final java.lang.Object listMeshNodes(systems.zlink.framework.locations.ZLinkMeshNodeLocationStore, java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkMeshNodeDescriptor>>);
  public static java.lang.Object listMeshNodes$default(systems.zlink.framework.locations.ZLinkMeshNodeLocationStore, java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object updateClientServer(systems.zlink.framework.locations.ZLinkClientServerLocationStore, systems.zlink.framework.locations.ZLinkClientServerServerDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  public static final java.lang.Object removeClientServer(systems.zlink.framework.locations.ZLinkClientServerLocationStore, systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteStatus>);
  public static final java.lang.Object listClientServers(systems.zlink.framework.locations.ZLinkClientServerLocationStore, java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkClientServerServerDescriptor>>);
  public static java.lang.Object listClientServers$default(systems.zlink.framework.locations.ZLinkClientServerLocationStore, java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object updateFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutLocationStore, systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  public static final java.lang.Object removeFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutLocationStore, systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteStatus>);
  public static final java.lang.Object listFanoutPublishers(systems.zlink.framework.locations.ZLinkFanoutLocationStore, java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor>>);
  public static java.lang.Object listFanoutPublishers$default(systems.zlink.framework.locations.ZLinkFanoutLocationStore, java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object updatePeer(systems.zlink.framework.locations.ZLinkPeerLocationStore, systems.zlink.framework.locations.ZLinkPeerLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  public static final java.lang.Object removePeer(systems.zlink.framework.locations.ZLinkPeerLocationStore, systems.zlink.framework.locations.ZLinkPeerLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  public static final java.lang.Object listPeerLocations(systems.zlink.framework.locations.ZLinkPeerLocationStore, systems.zlink.framework.locations.ZLinkPeerLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkPeerLocation>>);
  public static java.lang.Object listPeerLocations$default(systems.zlink.framework.locations.ZLinkPeerLocationStore, systems.zlink.framework.locations.ZLinkPeerLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object updateRoute(systems.zlink.framework.locations.ZLinkRouteLocationStore, systems.zlink.framework.locations.ZLinkRouteLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  public static final java.lang.Object removeRoute(systems.zlink.framework.locations.ZLinkRouteLocationStore, systems.zlink.framework.locations.ZLinkRouteLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationWriteResult>);
  public static final java.lang.Object resolveRoute(systems.zlink.framework.locations.ZLinkRouteLocationStore, systems.zlink.framework.locations.ZLinkRouteLocationKey, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkRouteLocation>);
  public static final java.lang.Object listRouteLocations(systems.zlink.framework.locations.ZLinkRouteLocationStore, systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkRouteLocation>>);
  public static java.lang.Object listRouteLocations$default(systems.zlink.framework.locations.ZLinkRouteLocationStore, systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object claimOwnerLease(systems.zlink.framework.locations.ZLinkLocationStore, java.lang.String, java.time.Duration, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult>);
  public static final java.lang.Object readOwnerLease(systems.zlink.framework.locations.ZLinkLocationStore, java.lang.String, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult>);
  public static final java.lang.Object renewOwnerLease(systems.zlink.framework.locations.ZLinkLocationStore, systems.zlink.framework.locations.ZLinkLocationOwnerToken, java.time.Duration, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult>);
  public static final java.lang.Object releaseOwnerLease(systems.zlink.framework.locations.ZLinkLocationStore, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult>);
  public static final java.lang.Object removeAllByOwner(systems.zlink.framework.locations.ZLinkLocationStore, systems.zlink.framework.locations.ZLinkLocationOwnerToken, kotlin.coroutines.Continuation<? super java.lang.Long>);
  public static final java.lang.Object listLivePeers(systems.zlink.framework.locations.ZLinkPeerLocationResolver, systems.zlink.framework.locations.ZLinkPeerLocationFilter, kotlin.coroutines.Continuation<? super java.util.List<systems.zlink.framework.locations.ZLinkPeerLocation>>);
  public static final java.lang.Object resolveSpotHandle(systems.zlink.framework.spots.SpotHandleResolver, java.lang.String, systems.zlink.contracts.core.RoutingId, kotlin.coroutines.Continuation<? super systems.zlink.framework.spots.SpotHandle>);
  public static final java.lang.Object resolveActorSpotHandle(systems.zlink.framework.spots.ActorSpotHandleResolver, java.lang.String, java.lang.String, kotlin.coroutines.Continuation<? super systems.zlink.framework.spots.SpotHandle>);
  public static final java.lang.Object status(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationRuntimeStatus>);
  public static final java.lang.Object listPeerLocations(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkPeerLocationFilter, kotlin.coroutines.Continuation<? super java.util.List<systems.zlink.framework.locations.ZLinkPeerLocation>>);
  public static final java.lang.Object listSpotLocations(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkSpotLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkSpotLocation>>);
  public static java.lang.Object listSpotLocations$default(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkSpotLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object listActorLocations(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkActorLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkActorLocation>>);
  public static java.lang.Object listActorLocations$default(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkActorLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object listRouteLocations(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkRouteLocation>>);
  public static java.lang.Object listRouteLocations$default(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object listTopology(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkLocationTopologyFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation<? super systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkLocationTopologyEntry>>);
  public static java.lang.Object listTopology$default(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkLocationTopologyFilter, systems.zlink.framework.locations.ZLinkPageRequest, kotlin.coroutines.Continuation, int, java.lang.Object);
  public static final java.lang.Object listServiceSummaries(systems.zlink.framework.locations.ZLinkLocationRuntimeQuery, systems.zlink.framework.locations.ZLinkLocationServiceSummaryFilter, kotlin.coroutines.Continuation<? super java.util.List<systems.zlink.framework.locations.ZLinkLocationServiceSummary>>);
  public static final kotlinx.coroutines.flow.Flow<systems.zlink.framework.locations.ZLinkLocationChanged> changes(systems.zlink.framework.locations.ZLinkLocationWatchStore, systems.zlink.framework.locations.ZLinkLocationWatchFilter);
  public static final <T> kotlinx.coroutines.flow.Flow<T> asFlow(java.util.concurrent.Flow$Publisher<T>);
}
public abstract class systems.zlink.framework.kotlin.ZLinkSuspendingLocationStore implements systems.zlink.framework.locations.ZLinkLocationStore, systems.zlink.framework.locations.ZLinkClientServerLocationStore, systems.zlink.framework.locations.ZLinkFanoutLocationStore, systems.zlink.framework.locations.ZLinkPeerLocationStore, systems.zlink.framework.locations.ZLinkRouteLocationStore {
  public systems.zlink.framework.kotlin.ZLinkSuspendingLocationStore(kotlinx.coroutines.CoroutineScope, kotlinx.coroutines.CoroutineDispatcher);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeMeshNode(systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkMeshNodeDescriptor>> listMeshNodes(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateClientServer(systems.zlink.framework.locations.ZLinkClientServerServerDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeClientServer(systems.zlink.framework.locations.ZLinkClientServerServerDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkClientServerServerDescriptor>> listClientServers(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteStatus> removeFanoutPublisher(systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptorKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor>> listFanoutPublishers(java.lang.String, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updatePeer(systems.zlink.framework.locations.ZLinkPeerLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> removePeer(systems.zlink.framework.locations.ZLinkPeerLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkPeerLocation>> listPeerLocations(systems.zlink.framework.locations.ZLinkPeerLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> updateRoute(systems.zlink.framework.locations.ZLinkRouteLocation, systems.zlink.framework.locations.ZLinkLocationWriteIntent);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationWriteResult> removeRoute(systems.zlink.framework.locations.ZLinkRouteLocationKey, systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkRouteLocation> resolveRoute(systems.zlink.framework.locations.ZLinkRouteLocationKey);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkLocationPage<systems.zlink.framework.locations.ZLinkRouteLocation>> listRouteLocations(systems.zlink.framework.locations.ZLinkRouteLocationFilter, systems.zlink.framework.locations.ZLinkPageRequest);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseClaimResult> claimOwnerLease(java.lang.String, java.time.Duration);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReadResult> readOwnerLease(java.lang.String);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseRenewResult> renewOwnerLease(systems.zlink.framework.locations.ZLinkLocationOwnerToken, java.time.Duration);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkOwnerLeaseReleaseResult> releaseOwnerLease(systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<java.lang.Long> removeAllByOwner(systems.zlink.framework.locations.ZLinkLocationOwnerToken);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityReadResult> read(java.lang.String, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityWriteResult> compareExchange(java.lang.String, systems.zlink.framework.locations.ZLinkAuthorityExpectation, systems.zlink.framework.locations.ZLinkAuthorityMutation, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public final java.util.concurrent.CompletionStage<systems.zlink.framework.locations.ZLinkAuthorityScanResult> list(java.lang.String, java.util.Optional<systems.zlink.framework.locations.ZLinkAuthorityScanCursor>, int, systems.zlink.framework.locations.ZLinkStoreCancellation);
  public systems.zlink.framework.kotlin.ZLinkSuspendingLocationStore();
}
```
