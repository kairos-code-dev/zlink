# Java 공통 runtime 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Host 종료 계약](../../../../54-graceful-drain-handoff.ko.md)

이 문서는 Java에서 host의 실행 상태, 종료 요청과 공통 비동기 operation을 표현하는 공개 타입을
고정한다. 공통 문서가 동작을 정의하며, 아래 선언은 Java에서 사용하는 타입과 member의 정확한 형태를
보여 준다.

```java
public enum ZLinkFrameworkRuntimeState {
    PREPARING(0), SERVING(1), RETIRING(2), DRAINING(3), STOPPED(4), ERROR(5);
    private final int wireValue;
    ZLinkFrameworkRuntimeState(int wireValue) { this.wireValue = wireValue; }
    public int wireValue() { return wireValue; }
}

public enum ZLinkTerminationIntent {
    RETIRE(0), SHUTDOWN(1);
    private final int wireValue;
    ZLinkTerminationIntent(int wireValue) { this.wireValue = wireValue; }
    public int wireValue() { return wireValue; }
}

public enum ZLinkTerminationOutcome {
    STOPPED(0), BLOCKED(1), FORCE_STOPPED(2);
    private final int wireValue;
    ZLinkTerminationOutcome(int wireValue) { this.wireValue = wireValue; }
    public int wireValue() { return wireValue; }
}

public enum ZLinkTerminationReason {
    NONE(0), TARGET_UNAVAILABLE(1), STORE_UNAVAILABLE(2),
    RELOCATION_DISABLED(3), STATE_INCOMPATIBLE(4),
    DEADLINE_EXCEEDED(5), RELOCATION_FAILED(6),
    TEARDOWN_FAILED(7), RUNTIME_NOT_READY(8),
    MANUAL_TOPOLOGY_UNSUPPORTED(9);
    private final int wireValue;
    ZLinkTerminationReason(int wireValue) { this.wireValue = wireValue; }
    public int wireValue() { return wireValue; }
}

public record ZLinkTerminationResult(
    ZLinkTerminationIntent effectiveIntent,
    ZLinkTerminationOutcome outcome,
    ZLinkTerminationReason reason) {}

public final class ZLinkFrameworkRuntime
    implements AutoCloseable, ZLinkMessageFlowControl, ZLinkDrainControl, ZLinkRuntimeQuery {
    public static final Duration DEFAULT_TERMINATION_DEADLINE = Duration.ofSeconds(30);

    public ZLinkClient client();
    public void setMessageFlowMode(ZLinkMessageFlowLogMode mode);
    public ZLinkMessageFlowLogMode messageFlowMode();
    public ZLinkFanoutClient fanout();
    public ZLinkRouteClient route();
    public ZLinkRouteMeshRuntime routeMeshRuntime();
    public ZLinkClientServerRuntime clientServerRuntime();
    public ZLinkFanoutRuntime fanoutRuntime();
    public ZLinkSpotManager spotManager();
    public ZLinkSpotOutbound spotOutbound();
    public ZLinkSpotPublisherClient spotPublisherClient();
    public ZLinkLocationRuntimeQuery monitoringLocationRuntimeQuery();
    public ZLinkLocationReadiness locationReadiness();
    public boolean stopSpotRuntime();
    public ZLinkActorManager actorManager();
    public ZLinkActorClient actorClient();
    public ZLinkSessionActorsRuntime sessionActors(String streamNodeName, RoutingId sessionRid);

    @Deprecated(since = "11.0", forRemoval = false)
    public CompletionStage<ZLinkTerminationResult> drain();
    @Deprecated(since = "11.0", forRemoval = false)
    public CompletionStage<ZLinkTerminationResult> drain(Duration deadline);
    @Deprecated(since = "11.0", forRemoval = false)
    public CompletionStage<ZLinkTerminationResult> awaitDrained();
    public boolean isReady();
    public ZLinkFrameworkRuntimeState state();
    public ZLinkFrameworkRuntimeSnapshot snapshot();
    public Flow.Publisher<ZLinkFrameworkRuntimeEvent> observe(int capacity);
    public CompletionStage<ZLinkTerminationResult> retire();
    public CompletionStage<ZLinkTerminationResult> retire(Duration deadline);
    public CompletionStage<ZLinkTerminationResult> shutdown();
    public CompletionStage<ZLinkTerminationResult> shutdown(Duration deadline);
    public void close();
}
```

`retire()`는 continuity preflight와 필요한 relocation을 수행한 뒤 host를 종료한다. User Spot은 [Spot](../../../../01-glossary.ko.md#spot)과 current
member Actor 전체를 하나의 bounded aggregate로 옮긴다. Aggregate participant 하나라도 `Disabled`이면
`Blocked/RelocationDisabled`, target·capacity·reservation을 확보할 수 없으면 `Blocked/TargetUnavailable`,
application version·type·Snapshot adapter capability가 맞지 않으면 `Blocked/StateIncompatible`로 끝난다. 이
preflight failure는 admission을 변경하지 않는다. [User Spot](../../../../01-glossary.ko.md#entry-spot-user-spot과-instance-spot)이 존재한다는 사실만으로 Retire를 차단하지 않는다.
Local manual RouteMesh peer, ClientServer client endpoint, fanout subscriber endpoint 또는 manual fanout publisher가
하나라도 있으면 `Blocked/ManualTopologyUnsupported`로 끝난다. Automatic RouteMesh는 source의 Core peer table에서
descriptor와 같은 RID·lifecycle generation이 admitted·ready가 된 뒤에만 `RETIRING`으로 전환한다.
`shutdown()`은 새 relocation을 시작하지 않는다. 두 operation 모두
숨은 remote `GetOrCreate`를 수행하지 않으며, waiter cancellation은 이미 시작한 shared operation을
취소하지 않는다. 각 호출은 shared operation 결과를 따르는 전용 `CompletableFuture` view를 반환한다.
`toCompletableFuture().cancel(...)`은 그 waiter만 해제하며 host operation은 계속 진행되고 다른 waiter는 같은
terminal 결과를 받는다. 별도 public cancellation token이나 host operation 취소 member를 추가하지 않는다.

모든 target을 `Prepared`로 만들고 `Draining` descriptor를 publish하기 전에 deadline이 먼저 끝나면 relocation
reference와 reservation을 durable abort 순서로 정리하고 source authority와 admission을 복원한 뒤
`Blocked/DeadlineExceeded`를 반환한다. `Draining` publish 뒤 [deadline](../../../../01-glossary.ko.md#deadline)은 source로 rollback하지 않고 bounded
teardown과 recovery handoff를 수행한 뒤 `ForceStopped/DeadlineExceeded`로 끝난다. 두 결과는 같은 reason을
사용하지만 phase와 side effect가 다르며 enum을 추가하지 않는다.

`ZLinkFrameworkRuntime`은 RouteMesh, ClientServer와 automatic fanout의 monitoring view를 각각 하나씩
소유한다. 세 accessor는 runtime 수명 동안 같은 객체를 반환하며 호출할 때 새 adapter를 만들지 않는다.
Spring starter가 제공하는 topology runtime bean도 이 accessor가 반환한 객체와 reference identity가 같다.

Spring starter는 `ZLinkFrameworkRuntime` bean을 제공한다. Host 종료의 정본은 `retire()`와 `shutdown()`이다.
Host-level `drain()`과 `awaitDrained()`는 source compatibility를 위한 deprecated facade이며 같은 shared
`shutdown()`의 `ZLinkTerminationResult`를 반환한다. MeshName을 받는 partial termination operation은 없다.
`SmartLifecycle`은 `shutdown()`을 사용하고 운영 maintenance endpoint는 `retire()`를 사용한다.

## Exact public member `javap` inventory

아래 선언은 `javap`가 출력하는 binary signature 형식으로 Java public type과 member를 고정한다.

```java
public final class systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState extends java.lang.Enum<systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState> {
  public static final systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState PREPARING;
  public static final systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState SERVING;
  public static final systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState DRAINING;
  public static final systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState STOPPED;
  public static final systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState ERROR;
  public int wireValue();
}
public final class systems.zlink.framework.runtime.host.ZLinkTerminationIntent extends java.lang.Enum<systems.zlink.framework.runtime.host.ZLinkTerminationIntent> {
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationIntent RETIRE;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationIntent SHUTDOWN;
  public int wireValue();
}
public final class systems.zlink.framework.runtime.host.ZLinkTerminationOutcome extends java.lang.Enum<systems.zlink.framework.runtime.host.ZLinkTerminationOutcome> {
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationOutcome STOPPED;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationOutcome BLOCKED;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationOutcome FORCE_STOPPED;
  public int wireValue();
}
public final class systems.zlink.framework.runtime.host.ZLinkTerminationReason extends java.lang.Enum<systems.zlink.framework.runtime.host.ZLinkTerminationReason> {
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason NONE;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason TARGET_UNAVAILABLE;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason STORE_UNAVAILABLE;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason RELOCATION_DISABLED;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason STATE_INCOMPATIBLE;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason DEADLINE_EXCEEDED;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason RELOCATION_FAILED;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason TEARDOWN_FAILED;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason RUNTIME_NOT_READY;
  public static final systems.zlink.framework.runtime.host.ZLinkTerminationReason MANUAL_TOPOLOGY_UNSUPPORTED;
  public int wireValue();
}
public final class systems.zlink.framework.runtime.host.ZLinkTerminationResult extends java.lang.Record {
  public systems.zlink.framework.runtime.host.ZLinkTerminationResult(systems.zlink.framework.runtime.host.ZLinkTerminationIntent, systems.zlink.framework.runtime.host.ZLinkTerminationOutcome, systems.zlink.framework.runtime.host.ZLinkTerminationReason);
  public systems.zlink.framework.runtime.host.ZLinkTerminationIntent effectiveIntent();
  public systems.zlink.framework.runtime.host.ZLinkTerminationOutcome outcome();
  public systems.zlink.framework.runtime.host.ZLinkTerminationReason reason();
}
public interface systems.zlink.framework.ZLinkMessageContext {
  public abstract java.util.Optional<java.lang.String> meshName();
  public abstract java.util.Optional<java.lang.String> channelName();
  public abstract java.lang.String packetName();
  public abstract java.util.Optional<java.lang.String> contentType();
  public abstract java.util.Map<java.lang.String, java.lang.String> metadata();
  public abstract java.util.Optional<java.lang.String> correlationId();
}
public interface systems.zlink.framework.ZLinkHandlerFilter {
  public abstract <T> java.util.concurrent.CompletionStage<T> invoke(systems.zlink.framework.ZLinkHandlerInvocation, systems.zlink.framework.ZLinkNext<T>);
}
public interface systems.zlink.framework.ZLinkHandlerInvocation {
  public abstract systems.zlink.framework.ZLinkMessageContext messageContext();
  public abstract java.util.Optional<java.lang.Object> request();
}
public interface systems.zlink.framework.ZLinkMessageSerializer {
  public abstract <T> systems.zlink.framework.ZLinkEncodedPayload serialize(T);
  public abstract <T> T deserialize(systems.zlink.framework.ZLinkEncodedPayload, java.lang.Class<T>);
  public default void prepare(java.lang.Class<?>);
}
public interface systems.zlink.framework.ZLinkNext<T> {
  public abstract java.util.concurrent.CompletionStage<T> invoke();
}
public final class systems.zlink.framework.errors.ZLinkFrameworkErrorKind extends java.lang.Enum<systems.zlink.framework.errors.ZLinkFrameworkErrorKind> {
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_ROUTE_NOT_FOUND;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_CREATE_FAILED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_ALREADY_EXISTS;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_TYPE_MISMATCH;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind SPOT_CREATE_FAILED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind SPOT_ROUTE_NOT_FOUND;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind SPOT_TYPE_MISMATCH;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_SESSION_NOT_BOUND;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind HANDLER_NOT_FOUND;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ROUTE_HANDLER_NOT_FOUND;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_DISPATCH_HANDLER_NOT_FOUND;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind PAYLOAD_DECODE_FAILED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ROUTE_NOT_CONNECTED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind REQUEST_TARGET_NOT_FOUND;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind REQUEST_REJECTED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind REQUEST_PROTOCOL_ERROR;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind REQUEST_FAILED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind WORKER_QUEUE_FULL;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind WORKER_TIMED_OUT;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind WORKER_FAILED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_LOCATION_STALE;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_CREATE_REJECTED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind OBJECT_CLIENT_NOT_CONFIGURED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind MESH_SELECTION_REQUIRED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind MESH_NOT_FOUND;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind INVALID_CONFIGURATION;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ALREADY_SUBMITTED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_GENERATION_STALE;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ACTOR_MOVING;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind DEADLINE_EXCEEDED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind PLACEMENT_CAPACITY_EXHAUSTED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind ROUTING_ID_CONFLICT;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind SPOT_GENERATION_STALE;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind SPOT_MOVING;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind RELOCATION_DATA_LOST;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind SPOT_ID_CONFLICT;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind RUNTIME_SHUTDOWN;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind RELOCATION_DISABLED;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind RELOCATION_TARGET_UNAVAILABLE;
  public static final systems.zlink.framework.errors.ZLinkFrameworkErrorKind RELOCATION_FAILED;
  public static systems.zlink.framework.errors.ZLinkFrameworkErrorKind[] values();
  public static systems.zlink.framework.errors.ZLinkFrameworkErrorKind valueOf(java.lang.String);
  public int value();
  public boolean retriable();
  public static systems.zlink.framework.errors.ZLinkFrameworkErrorKind fromValue(int);
}
public class systems.zlink.framework.errors.ZLinkFrameworkException extends java.lang.RuntimeException {
  public systems.zlink.framework.errors.ZLinkFrameworkException(java.lang.String);
  public systems.zlink.framework.errors.ZLinkFrameworkException(java.lang.String, java.lang.Throwable);
  public systems.zlink.framework.errors.ZLinkFrameworkException(systems.zlink.framework.errors.ZLinkFrameworkErrorKind, java.lang.String);
  public systems.zlink.framework.errors.ZLinkFrameworkException(systems.zlink.framework.errors.ZLinkFrameworkErrorKind, java.lang.String, java.lang.Throwable);
  public systems.zlink.framework.errors.ZLinkFrameworkException(systems.zlink.framework.errors.ZLinkFrameworkErrorKind, java.lang.String, java.lang.Boolean, java.lang.Throwable);
  public systems.zlink.framework.errors.ZLinkFrameworkErrorKind kind();
  public boolean retriable();
}
```

`ZLinkFrameworkErrorKind.value()`는 선언 순서와 무관하게 공통 숫자를 반환한다. 기존 kind 0..21 뒤에
`OBJECT_CLIENT_NOT_CONFIGURED=22`, `MESH_SELECTION_REQUIRED=23`, `MESH_NOT_FOUND=24`,
`INVALID_CONFIGURATION=25`, `ALREADY_SUBMITTED=26`, `ACTOR_GENERATION_STALE=27`, `ACTOR_MOVING=28`,
`DEADLINE_EXCEEDED=29`, `PLACEMENT_CAPACITY_EXHAUSTED=30`, `ROUTING_ID_CONFLICT=31`,
`SPOT_GENERATION_STALE=32`, `SPOT_MOVING=33`, `RELOCATION_DATA_LOST=34`,
`SPOT_ID_CONFLICT=35`, `RUNTIME_SHUTDOWN=36`, `RELOCATION_DISABLED=37`,
`RELOCATION_TARGET_UNAVAILABLE=38`, `RELOCATION_FAILED=39`를 고정한다.
`ROUTING_ID_CONFLICT`는 MeshNode RID 충돌에만 사용하고
Spot·Entry Spot identity 충돌은 `SPOT_ID_CONFLICT`로 반환한다. `fromValue(int)`도 같은
mapping을 사용한다. `RELOCATION_DATA_LOST`는 Location [authority](../../../../01-glossary.ko.md#authority)가 공개한 Relocation payload가 영구적으로
없거나 checksum·inventory digest가 일치하지 않을 때 반환하며 재시도하거나 이전 owner로 rollback하지 않는다.
One-way operation의 target 부재는 Actor·Spot·Mesh·request·session별 기존 not-found kind를 사용한다.
Runtime 종료만 공통 `RUNTIME_SHUTDOWN`으로 투영한다.

## Serializer와 오류 public signature

```java
public final class systems.zlink.framework.ZLinkEncodedPayload {
  public static systems.zlink.framework.ZLinkEncodedPayload from(byte[]);
  public byte[] bytes();
}
public final class systems.zlink.framework.errors.ZLinkConfigurationException extends systems.zlink.framework.errors.ZLinkFrameworkException {
  public systems.zlink.framework.errors.ZLinkConfigurationException(java.lang.String);
  public systems.zlink.framework.errors.ZLinkConfigurationException(java.lang.String, java.lang.Throwable);
  public systems.zlink.framework.errors.ZLinkConfigurationException(systems.zlink.framework.errors.ZLinkFrameworkErrorKind, java.lang.String);
}
public final class systems.zlink.framework.errors.ZLinkOperationCanceledException extends systems.zlink.framework.errors.ZLinkFrameworkException {
  public systems.zlink.framework.errors.ZLinkOperationCanceledException(java.lang.String);
}
public class systems.zlink.framework.errors.ZLinkWorkerFailedException extends systems.zlink.framework.errors.ZLinkFrameworkException {
  public systems.zlink.framework.errors.ZLinkWorkerFailedException(java.lang.String, java.lang.Throwable);
}
public class systems.zlink.framework.errors.ZLinkWorkerQueueFullException extends systems.zlink.framework.errors.ZLinkFrameworkException {
  public systems.zlink.framework.errors.ZLinkWorkerQueueFullException(java.lang.String);
}
public class systems.zlink.framework.errors.ZLinkWorkerTimeoutException extends systems.zlink.framework.errors.ZLinkFrameworkException {
  public systems.zlink.framework.errors.ZLinkWorkerTimeoutException(java.lang.String);
}
public final class systems.zlink.framework.messaging.ZLinkMessage {
  public static systems.zlink.framework.messaging.ZLinkMessage empty();
  public static systems.zlink.framework.messaging.ZLinkMessage of(java.lang.Object);
  public static systems.zlink.framework.messaging.ZLinkMessage fromEncoded(systems.zlink.framework.ZLinkEncodedPayload, systems.zlink.framework.ZLinkMessageSerializer);
  public boolean isEmpty();
  public <T> T decode(java.lang.Class<T>);
  public systems.zlink.framework.ZLinkEncodedPayload toEncodedPayload(systems.zlink.framework.ZLinkMessageSerializer);
}
public final class systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec implements systems.zlink.framework.configuration.ZLinkCodecExtension,systems.zlink.stream.connector.ZLinkStreamTypedCodec {
  public static systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec defaultCodec();
  public static systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec forPayloadTypes(java.util.function.Predicate<java.lang.Class<?>>);
  public <T> systems.zlink.stream.connector.ZLinkStreamEncodedPayload encode(java.lang.String, T);
  public <T> T decode(systems.zlink.stream.connector.ZLinkStreamEncodedPayload, java.lang.Class<T>);
  public void register(systems.zlink.framework.configuration.ZLinkCodecRegistrar);
}
public final class systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec implements systems.zlink.framework.configuration.ZLinkCodecExtension,systems.zlink.stream.connector.ZLinkStreamTypedCodec {
  public static systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec defaultCodec();
  public <T> systems.zlink.stream.connector.ZLinkStreamEncodedPayload encode(java.lang.String, T);
  public <T> T decode(systems.zlink.stream.connector.ZLinkStreamEncodedPayload, java.lang.Class<T>);
  public void register(systems.zlink.framework.configuration.ZLinkCodecRegistrar);
}
```
