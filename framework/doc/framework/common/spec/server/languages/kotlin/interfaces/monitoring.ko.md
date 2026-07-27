# Kotlin monitoring 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java monitoring](../../java/interfaces/monitoring.ko.md)

Kotlin은 Java의 RouteMesh, ClientServer, automatic fanout과 host runtime snapshot·event·lifecycle 타입을
사용한다. `Flow` projection은 Java publisher를 coroutine cancellation에 연결할 뿐 별도 state 또는 event value를
정의하지 않는다.

Topology runtime은 Java `ZLinkFrameworkRuntime`의 `routeMeshRuntime()`, `clientServerRuntime()`과
`fanoutRuntime()`을 그대로 사용한다. Kotlin wrapper accessor를 추가하지 않으며 Spring에서 주입받은 topology
bean은 해당 Java accessor의 반환값과 reference identity가 같다.
Java monitoring 계약과 마찬가지로 MeshNode snapshot과 runtime event에는 Logical Multicast 통계,
publish target 수 또는 target별 수락·실패 field가 없다. Kotlin 전용 projection으로 이를 추가하지 않는다.

ClientServer server 상태는 Java `ZLinkClientServerServerState`, fanout publisher 연결 상태는
`ZLinkFanoutPublisherConnectionState`를 그대로 사용한다. Host의 `ZLinkFrameworkRuntimeState`나 MeshNode의
`ZLinkTopologyState`를 이 두 connection 상태에 대신 사용하지 않는다.
같은 ChannelName에 Client와 Server를 함께 등록한 [snapshot](../../../../01-glossary.ko.md#snapshot)의 local role은 Java
`ZLinkClientServerRole.CLIENT_AND_SERVER`로 나타낸다. 이는 별도 role registration 두 개의 aggregate
projection일 뿐 builder role이나 registration key가 아니다. Kotlin 전용 enum이나 변환 값을 만들지 않는다.

Fanout ready 의미도 Java 계약을 그대로 사용한다. Publisher 전용 SUB socket의 native-ready만으로 [ready](../../../../01-glossary.ko.md#ready)가
되지 않으며, 같은 socket에서 첫 valid application record 또는 liveness beacon까지 받아야 한다. 15초 inbound
timeout은 해당 publisher entry만 `DISCONNECTED`로 바꾼다.

[RouteMesh](../../../../01-glossary.ko.md#routemesh) object placement snapshot은 node-wide placement weight, active object 수와 상한, pending activation
수와 상한을 Java의 typed numeric field로 제공한다. Channel [weight](../../../../01-glossary.ko.md#weight)와 같은 field로 합치지 않는다. Object
location snapshot은 global ActorId 또는 SpotId, stable type, object generation, MeshName과 NodeRid를 제공한다.
전체 object directory나 process-local handle은 monitoring surface에 포함하지 않는다.

## Framework 오류 값

Kotlin은 Java `ZLinkFrameworkErrorKind`를 그대로 사용한다. Enum 이름과 숫자는 wire와 public exception
분류의 일부이며 다음 값을 고정한다. 기존 값 0..21은 유지한다.

```text
OBJECT_CLIENT_NOT_CONFIGURED = 22
MESH_SELECTION_REQUIRED = 23
MESH_NOT_FOUND = 24
INVALID_CONFIGURATION = 25
ALREADY_SUBMITTED = 26
ACTOR_GENERATION_STALE = 27
ACTOR_MOVING = 28
DEADLINE_EXCEEDED = 29
PLACEMENT_CAPACITY_EXHAUSTED = 30
ROUTING_ID_CONFLICT = 31
SPOT_GENERATION_STALE = 32
SPOT_MOVING = 33
RELOCATION_DATA_LOST = 34
SPOT_ID_CONFLICT = 35
RUNTIME_SHUTDOWN = 36
RELOCATION_DISABLED = 37
RELOCATION_TARGET_UNAVAILABLE = 38
RELOCATION_FAILED = 39
```

`RELOCATION_DATA_LOST`는 Location authority가 공개한 Relocation payload가 영구적으로 없거나 checksum·inventory
digest가 일치하지 않을 때 반환하는 non-retriable 오류다. Runtime은 이전 owner로 rollback하지 않는다.
`ROUTING_ID_CONFLICT`는 MeshNode RID 충돌에만 사용한다. Spot·Entry Spot identity 충돌은
`SPOT_ID_CONFLICT`로 반환한다.
Remote framework error는 `ZLinkFrameworkException`으로 전달한다. Public argument validation은 JVM 표준
`IllegalArgumentException`, startup 구성 충돌은 `ZLinkConfigurationException`을 사용한다.

## Kotlin source signature

```kotlin
fun ZLinkDispatchOptions.onMessageFlow(
    observer: (ZLinkMessageFlowEvent) -> Unit,
): ZLinkDispatchOptions
```

Java `Publisher` event stream을 Kotlin `Flow`로 읽을 때는
[Location과 maintenance](location-maintenance.ko.md)가 소유하는 공통 `asFlow()` bridge를 사용한다.
이 bridge의 cancellation은 해당 subscriber 등록만 해제한다. 공유 runtime, monitoring publisher
또는 이미 시작한 host operation을 취소하지 않는다. `onMessageFlow` generated JVM member는
[구성과 host](configuration-host.ko.md)의 multifile class inventory에 포함한다.
