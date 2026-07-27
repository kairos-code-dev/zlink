# Kotlin Location과 relocation 공개 인터페이스

[Kotlin exact interface 목록](README.ko.md) · [Java Location contract](../../java/interfaces/location-maintenance.ko.md)

Kotlin은 Java runtime과 provider SPI를 그대로 사용한다. 별도 Kotlin Store interface나 abstract Store base class를
정의하지 않는다. Provider는 Java `ZLinkLocationStore` 또는 `ZLinkRelocationStore`를 구현하고
`ZLinkFrameworkOptions`에 등록한다.

Kotlin package는 Java `CompletionStage`를 기다리는 coroutine projection만 제공한다. 이 projection은 Store의
transaction 경계, error 분류, cancellation 의미를 바꾸지 않는다.

```kotlin
suspend fun ZLinkLocationRuntimeQuery.status(): ZLinkLocationRuntimeStatus

suspend fun ZLinkLocationRuntimeQuery.listMeshNodes(
    meshName: String,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkMeshNodeDescriptor>

suspend fun ZLinkLocationRuntimeQuery.listTopology(
    filter: ZLinkLocationTopologyFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkLocationTopologyEntry>

suspend fun ZLinkLocationRuntimeQuery.listServiceSummaries(
    filter: ZLinkLocationServiceSummaryFilter,
    page: ZLinkPageRequest = ZLinkPageRequest.firstPage(),
): ZLinkLocationPage<ZLinkLocationServiceSummary>

fun ZLinkLocationRuntimeQuery.meshNodes(
    meshName: String,
    pageSize: Int = 100,
): Flow<ZLinkMeshNodeDescriptor>

fun ZLinkLocationRuntimeQuery.topology(
    filter: ZLinkLocationTopologyFilter,
    pageSize: Int = 100,
): Flow<ZLinkLocationTopologyEntry>
```

Provider mutation을 `suspend` 함수로 한 번 더 복제하지 않는다. Java SPI 하나를 유지하면 Java와 Kotlin
provider가 같은 ABI와 contract test를 공유하고, coroutine scheduling이 storage transaction 의미에 섞이지 않는다.
Provider가 구현 과정에서 직접 사용하는 descriptor, request, fence와 sealed result type의 정확한 범위도
Java Location contract의 transitive public boundary와 같다. Kotlin package는 이 타입들을 data class나 별도
result hierarchy로 복제하지 않는다.

Watch publisher, 별도 change-stamp Store나 event, raw peer·Spot·Actor·route Store, serializer 선택 helper, routing-ID slot/group와
allocated-RID provider는 Kotlin public contract가 아니다. Redis extension도 Java의 options와 두 Store class를
그대로 사용하며 Kotlin 전용 wrapper를 추가하지 않는다.
