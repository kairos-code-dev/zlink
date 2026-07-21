# Kotlin monitoring 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Java monitoring](../../java/interfaces/monitoring.ko.md)

Kotlin은 Java의 RouteMesh, ClientServer, automatic fanout과 host runtime snapshot·event·lifecycle 타입을
사용한다. `Flow` projection은 Java publisher를 coroutine cancellation에 연결할 뿐 별도 state 또는 event value를
정의하지 않는다.

Topology runtime은 Java `ZLinkFrameworkRuntime`의 `routeMeshRuntime()`, `clientServerRuntime()`과
`fanoutRuntime()`을 그대로 사용한다. Kotlin wrapper accessor를 추가하지 않으며 Spring에서 주입받은 topology
bean은 해당 Java accessor의 반환값과 reference identity가 같다.

ClientServer server 상태는 Java `ZLinkClientServerServerState`, fanout publisher 연결 상태는
`ZLinkFanoutPublisherConnectionState`를 그대로 사용한다. Host의 `ZLinkFrameworkRuntimeState`나 MeshNode의
`ZLinkMeshNodeState`를 이 두 connection 상태에 대신 사용하지 않는다.

Fanout ready 의미도 Java 계약을 그대로 사용한다. Publisher 전용 SUB socket의 native-ready만으로 ready가
되지 않으며, 같은 socket에서 첫 valid application record 또는 liveness beacon까지 받아야 한다. 15초 inbound
timeout은 해당 publisher entry만 `DISCONNECTED`로 바꾼다.

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
