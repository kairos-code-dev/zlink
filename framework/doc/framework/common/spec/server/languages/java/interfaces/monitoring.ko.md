# Java monitoring 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Runtime monitoring](../../../../50-runtime-monitoring.ko.md)

Monitoring DTO는 Framework lifecycle state와 공통 wire 값을 사용한다. Raw monitor handle, native event enum,
socket address와 connection ID는 bounded diagnostic에 필요한 범위 밖으로 노출하지 않는다.

```java
public enum ZLinkMeshNodeState {
    STARTING,
    SERVING,
    DRAINING,
    DRAINED,
    FORCE_STOPPING,
    STOPPED,
    FAULTED
}

public interface ZLinkRouteMeshRuntime {
    ZLinkMeshNodeSnapshot snapshot(String meshName);
    Flow.Publisher<ZLinkMeshRuntimeEvent> observe(
        String meshName, int capacity);
    boolean isReady(String meshName);
}

public interface ZLinkRuntimeErrorSink {
    CompletionStage<Void> onRuntimeError(ZLinkRuntimeErrorEvent error);
}

public interface ZLinkMonitoringOptionsCustomizer {
    void customize(ZLinkMonitoringOptions options);
}
```

Orderly disconnect와 service liveness timeout은 다른 reason으로 관측한다. Peer 하나가 실패해도 다른 ready
peer와 host를 `ERROR`로 바꾸지 않는다.

별도 drain control과 component termination result는 제공하지 않는다. Object relocation과 host 종료는
`ZLinkFrameworkRuntime`의 `relocate(options)`와 `shutdown()`이 각각 조정한다. Relocation options는
planned maintenance와 rolling update의 target version 규칙을 명시하며 monitoring API가 target을
추론하거나 변경하지 않는다.

## Host runtime observation exact source signature

Host runtime observation은 application이 import해 사용하는 Java source 형태로 고정한다. 아래처럼
package와 import를 먼저 명시하므로 member signature에는 fully-qualified type을 반복하지 않는다.
Record component는 같은 이름과 타입의 public accessor를 생성하며 accessor를 본문에 반복 선언하지 않는다.
코드 블록은 같은 package에 있는 선언을 함께 보여 주지만, 각 public top-level type은 별도
Java source file에 위치한다.

```java
package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.Flow;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRelocationResult;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;
import systems.zlink.framework.runtime.host.ZLinkFrameworkTerminationResult;

public record ZLinkFrameworkRuntimeStatus(
    ZLinkFrameworkRuntimeState state,
    boolean isReady,
    boolean acceptingWork,
    Optional<Instant> deadline,
    Optional<ZLinkFrameworkRelocationResult> relocationResult,
    Optional<ZLinkFrameworkTerminationResult> terminationResult,
    long sequence,
    Instant observedAt) {}
```

## Topology runtime observation exact source signature

ClientServer와 automatic fanout은 ChannelName으로 snapshot과 event를 조회한다. [Snapshot](../../../../01-glossary.ko.md#snapshot) 조회는 connection이나
target selection을 바꾸지 않는다. Kotlin도 아래 Java 타입을 그대로 사용한다.

```java
package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.Flow;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkInstanceSpotTypeSnapshot(
    String instanceSpotType,
    long activeCount,
    long activatingCount,
    long closingCount,
    long pendingMessageCount,
    long pendingByteCount,
    Optional<String> lastActivationOutcome) {}

public enum ZLinkClientServerRole {
    CLIENT,
    SERVER,
    CLIENT_AND_SERVER
}

public enum ZLinkClientServerServerState {
    CONFIGURED,
    CONNECTING,
    READY,
    DRAINING,
    DISCONNECTED,
    REJECTED
}

public record ZLinkClientServerServerSnapshot(
    RoutingId serverRid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    int weight,
    boolean ready,
    ZLinkClientServerServerState state,
    String descriptorSource,
    Optional<String> lastFailure) {}

public record ZLinkClientServerChannelSnapshot(
    String channelName,
    ZLinkClientServerRole localRole,
    boolean selectable,
    int readyServerCount,
    int connectionIntentCount,
    int pendingRequestCount,
    long sequence,
    Instant observedAt,
    List<ZLinkClientServerServerSnapshot> servers,
    ZLinkLocationRuntimeSnapshot location) {}

public record ZLinkClientServerRuntimeEvent(
    String identifier,
    long sequence,
    Instant timestamp,
    String channelName,
    Optional<RoutingId> serverRid,
    Optional<Long> lifecycleGeneration,
    Optional<Long> descriptorRevision,
    Optional<Integer> weight,
    Optional<Boolean> ready,
    Optional<ZLinkClientServerServerState> state,
    Optional<String> reason) {}

public interface ZLinkClientServerRuntime {
    ZLinkClientServerChannelSnapshot snapshot(String channelName);
    Flow.Publisher<ZLinkClientServerRuntimeEvent> observe(
        String channelName, int capacity);
    boolean isReady(String channelName);
}

public enum ZLinkFanoutPublisherConnectionState {
    CONNECTING,
    READY,
    DISCONNECTED,
    RECONNECTING,
    EXCLUDED_DRAINING,
    EXCLUDED_STALE
}

public record ZLinkFanoutPublisherConnectionSnapshot(
    RoutingId publisherRid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    boolean connectionIntent,
    boolean ready,
    ZLinkFanoutPublisherConnectionState state,
    Optional<String> lastFailure) {}

public record ZLinkFanoutChannelSnapshot(
    String channelName,
    int connectionIntentCount,
    int readyConnectionCount,
    long sequence,
    Instant observedAt,
    List<ZLinkFanoutPublisherConnectionSnapshot> publishers,
    ZLinkLocationRuntimeSnapshot location) {}

public sealed interface ZLinkFanoutRuntimeEvent
    permits ZLinkFanoutRuntimeEvent.PublisherChanged,
            ZLinkFanoutRuntimeEvent.LocationChanged {
    String identifier();
    long sequence();
    Instant timestamp();
    String channelName();

    record PublisherChanged(
        long sequence,
        Instant timestamp,
        String channelName,
        ZLinkFanoutPublisherConnectionSnapshot entry)
        implements ZLinkFanoutRuntimeEvent {
        @Override public String identifier() {
            return "zlink.runtime.fanout.publisher_changed";
        }
    }

    record LocationChanged(
        long sequence,
        Instant timestamp,
        String channelName,
        ZLinkLocationRuntimeSnapshot location)
        implements ZLinkFanoutRuntimeEvent {
        @Override public String identifier() {
            return "zlink.runtime.location.store_changed";
        }
    }
}

public interface ZLinkFanoutRuntime {
    ZLinkFanoutChannelSnapshot snapshot(String channelName);
    Flow.Publisher<ZLinkFanoutRuntimeEvent> observe(
        String channelName, int capacity);
}
```

같은 [ChannelName](../../../../01-glossary.ko.md#channelname)에 Client와 Server를 함께 등록한 snapshot의 `localRole`은
`ZLinkClientServerRole.CLIENT_AND_SERVER`다. 이 값은 `(ChannelName, Role)`의 별도 registration 두 개가
하나의 ClientServer topology를 공유한다는 aggregate projection이다. Builder에서 선택하거나 registration
key로 사용할 수 없다.

`ZLinkInstanceSpotTypeSnapshot`은 startup에 등록한 Instance type별 aggregate다. 개별 Spot ID, owner ID,
`ObjectGeneration`, `AuthorityOwnerGeneration`, `StoreVersion`과 owner lease fence는 포함하지 않는다.
`lastActivationOutcome`은 `ready`, `rejected`, `conflict`, `timed_out`,
`shutdown`, `store_failure`, `fenced` 가운데 하나이며 아직 terminal activation이 없으면 비어 있다.

`ZLinkRouteMeshRuntime`, `ZLinkClientServerRuntime`과 `ZLinkFanoutRuntime`은 Spring starter가 singleton bean으로
제공한다. 각 bean은 `ZLinkFrameworkRuntime`의 대응 accessor가 반환한 객체와 reference identity가 같다. Manual
fanout subscriber는 endpoint connection 표면을 사용하므로 `ZLinkFanoutRuntime` 조회 대상이 아니다. Observer
capacity는 0보다 커야 하며 publisher cancellation은 해당 관찰만 끝낸다.

`ZLinkMeshNodeSnapshot.objectCapacity()`는 Actor 전체, Spot 전체와 등록한 User·Instance Spot type별
`active`, `reserved`, `limit`을 반환한다. Limit `0`은 제한이 없다는 뜻이다. Entry Spot은 Spot 집계에서
제외하지만 Entry Spot에 존재하는 Actor는 Actor 전체 집계에 포함한다. `activationConcurrency()`는 현재
factory·initialization 실행 수와 양수 limit을 별도 값으로 반환하며 population capacity에 합치지 않는다.
Host status는 readiness, 신규 work 수락 여부와 relocation·termination terminal 결과만 제공한다.
Work seal, pending relocation 수와 STREAM barrier 수는 Framework 내부 조정 상태이므로 공개하지 않는다.

Fanout의 `connectionIntent`는 automatic planner의 endpoint intent다. `ready`와 `readyConnectionCount`는
publisher 전용 SUB socket의 native-ready와 같은 socket의 첫 valid application record 또는 liveness beacon
수신을 모두 관찰한 뒤에만 증가한다. Native disconnect 또는 15초 inbound timeout은 해당 publisher entry만
`DISCONNECTED`로 바꾼다.

위의 host와 topology exact source signature가 11.0 monitoring public type의 정본이다. 배포 package
검증은 각 source type의 생성된 accessor와 method를 `javap`로 대조하지만, 그 출력을 이 문서에
반복하지 않는다.

아래 inventory는 위 exact source signature에서 선언하지 않은 나머지 배포 symbol을 `javap`
signature 형식으로 대조한다. 이 형식은 symbol의 package를 검증해야 하므로 fully-qualified
name을 유지한다.

## 나머지 배포 symbol `javap` inventory

아래 선언은 위 source signature와 중복되지 않는 Java public type과 member를 고정한다.

```java
public final class systems.zlink.framework.monitoring.ZLinkMeshNodeState extends java.lang.Enum<systems.zlink.framework.monitoring.ZLinkMeshNodeState> {
  public static final systems.zlink.framework.monitoring.ZLinkMeshNodeState STARTING;
  public static final systems.zlink.framework.monitoring.ZLinkMeshNodeState SERVING;
  public static final systems.zlink.framework.monitoring.ZLinkMeshNodeState DRAINING;
  public static final systems.zlink.framework.monitoring.ZLinkMeshNodeState DRAINED;
  public static final systems.zlink.framework.monitoring.ZLinkMeshNodeState FORCE_STOPPING;
  public static final systems.zlink.framework.monitoring.ZLinkMeshNodeState STOPPED;
  public static final systems.zlink.framework.monitoring.ZLinkMeshNodeState FAULTED;
  public static systems.zlink.framework.monitoring.ZLinkMeshNodeState[] values();
  public static systems.zlink.framework.monitoring.ZLinkMeshNodeState valueOf(java.lang.String);
}
public interface systems.zlink.framework.monitoring.ZLinkRouteMeshRuntime {
  public abstract systems.zlink.framework.monitoring.ZLinkMeshNodeSnapshot snapshot(java.lang.String);
  public abstract java.util.concurrent.Flow$Publisher<systems.zlink.framework.monitoring.ZLinkMeshRuntimeEvent> observe(java.lang.String, int);
  public abstract boolean isReady(java.lang.String);
}
public interface systems.zlink.framework.monitoring.ZLinkRuntimeErrorSink {
  public abstract java.util.concurrent.CompletionStage<java.lang.Void> onRuntimeError(systems.zlink.framework.monitoring.ZLinkRuntimeErrorEvent);
}
public interface systems.zlink.framework.spring.ZLinkMonitoringOptionsCustomizer {
  public abstract void customize(systems.zlink.framework.monitoring.ZLinkMonitoringOptions);
}
public final class systems.zlink.framework.monitoring.ZLinkLocationRuntimeEvent extends java.lang.Record implements systems.zlink.framework.monitoring.ZLinkRuntimeEvent {
  public systems.zlink.framework.monitoring.ZLinkLocationRuntimeEvent(java.lang.String, java.time.Instant, systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind, systems.zlink.framework.locations.ZLinkLocationRuntimeStatus, java.util.List<systems.zlink.framework.locations.ZLinkLocationTopologyEntry>, java.util.List<systems.zlink.framework.locations.ZLinkLocationServiceSummary>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String sourceName();
  public java.time.Instant timestamp();
  public systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind event();
  public systems.zlink.framework.locations.ZLinkLocationRuntimeStatus status();
  public java.util.List<systems.zlink.framework.locations.ZLinkLocationTopologyEntry> topology();
  public java.util.List<systems.zlink.framework.locations.ZLinkLocationServiceSummary> serviceSummary();
}
public final class systems.zlink.framework.monitoring.ZLinkLocationRuntimeSnapshot extends java.lang.Record {
  public systems.zlink.framework.monitoring.ZLinkLocationRuntimeSnapshot(java.lang.String, java.util.Optional<java.time.Instant>, java.util.Optional<java.time.Instant>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String state();
  public java.util.Optional<java.time.Instant> lastSuccessAt();
  public java.util.Optional<java.time.Instant> lastFailureAt();
}
public final class systems.zlink.framework.monitoring.ZLinkMeshChannelSnapshot extends java.lang.Record {
  public systems.zlink.framework.monitoring.ZLinkMeshChannelSnapshot(java.lang.String, int, long, boolean);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String channelName();
  public int localWeight();
  public long readyMemberCount();
  public boolean selectable();
}
public final class systems.zlink.framework.monitoring.ZLinkMeshClaimSnapshot extends java.lang.Record {
  public systems.zlink.framework.monitoring.ZLinkMeshClaimSnapshot(boolean, long, boolean, long);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public boolean applicationActive();
  public long pendingApplicationWork();
  public boolean infrastructureActive();
  public long pendingInfrastructureWork();
}
public final class systems.zlink.framework.monitoring.ZLinkMeshNodeSnapshot extends java.lang.Record {
  public systems.zlink.framework.monitoring.ZLinkMeshNodeSnapshot(java.lang.String, systems.zlink.contracts.core.RoutingId, long, long, java.lang.String, systems.zlink.framework.monitoring.ZLinkMeshNodeState, long, java.time.Instant, java.util.List<java.lang.String>, java.util.List<systems.zlink.framework.monitoring.ZLinkMeshPeerSnapshot>, java.util.List<systems.zlink.framework.monitoring.ZLinkMeshChannelSnapshot>, java.util.List<systems.zlink.framework.monitoring.ZLinkInstanceSpotTypeSnapshot>, systems.zlink.framework.monitoring.ZLinkMeshClaimSnapshot, systems.zlink.framework.monitoring.ZLinkLocationRuntimeSnapshot, systems.zlink.framework.locations.ZLinkMeshNodeObjectRole, int, systems.zlink.framework.locations.ZLinkPlacementCapacity, systems.zlink.framework.locations.ZLinkActivationConcurrency, java.util.List<systems.zlink.framework.locations.ZLinkObjectCapability>, long, java.util.Optional<java.lang.String>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String meshName();
  public systems.zlink.contracts.core.RoutingId rid();
  public long lifecycleGeneration();
  public long descriptorRevision();
  public java.lang.String endpoint();
  public systems.zlink.framework.monitoring.ZLinkMeshNodeState state();
  public long sequence();
  public java.time.Instant observedAt();
  public java.util.List<java.lang.String> descriptorSources();
  public java.util.List<systems.zlink.framework.monitoring.ZLinkMeshPeerSnapshot> peers();
  public java.util.List<systems.zlink.framework.monitoring.ZLinkMeshChannelSnapshot> channels();
  public java.util.List<systems.zlink.framework.monitoring.ZLinkInstanceSpotTypeSnapshot> instanceSpots();
  public systems.zlink.framework.monitoring.ZLinkMeshClaimSnapshot claims();
  public systems.zlink.framework.monitoring.ZLinkLocationRuntimeSnapshot location();
  public systems.zlink.framework.locations.ZLinkMeshNodeObjectRole objectRole();
  public int placementWeight();
  public systems.zlink.framework.locations.ZLinkPlacementCapacity objectCapacity();
  public systems.zlink.framework.locations.ZLinkActivationConcurrency activationConcurrency();
  public java.util.List<systems.zlink.framework.locations.ZLinkObjectCapability> objectCapabilities();
  public long placementReservationFailureCount();
  public java.util.Optional<java.lang.String> lastPlacementReservationFailure();
}
public final class systems.zlink.framework.monitoring.ZLinkMeshPeerSnapshot extends java.lang.Record {
  public systems.zlink.framework.monitoring.ZLinkMeshPeerSnapshot(systems.zlink.contracts.core.RoutingId, long, long, java.lang.String, java.lang.String, boolean, java.lang.String, java.util.List<java.lang.String>, java.util.Optional<java.lang.String>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public systems.zlink.contracts.core.RoutingId rid();
  public long lifecycleGeneration();
  public long descriptorRevision();
  public java.lang.String endpoint();
  public java.lang.String admissionState();
  public boolean ready();
  public java.lang.String drainState();
  public java.util.List<java.lang.String> channelNames();
  public java.util.Optional<java.lang.String> lastFailure();
}
public final class systems.zlink.framework.monitoring.ZLinkMeshRuntimeEvent extends java.lang.Record {
  public systems.zlink.framework.monitoring.ZLinkMeshRuntimeEvent(java.lang.String, long, java.time.Instant, java.lang.String, systems.zlink.contracts.core.RoutingId, java.util.Optional<systems.zlink.contracts.core.RoutingId>, java.util.Optional<java.lang.Long>, java.util.Optional<java.lang.Long>, java.util.Optional<java.lang.String>, java.util.Optional<java.lang.String>, java.util.Optional<java.lang.String>, java.util.Optional<java.lang.String>, java.util.Optional<systems.zlink.framework.monitoring.ZLinkMeshNodeState>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String identifier();
  public long sequence();
  public java.time.Instant timestamp();
  public java.lang.String meshName();
  public systems.zlink.contracts.core.RoutingId sourceRid();
  public java.util.Optional<systems.zlink.contracts.core.RoutingId> peerRid();
  public java.util.Optional<java.lang.Long> lifecycleGeneration();
  public java.util.Optional<java.lang.Long> descriptorRevision();
  public java.util.Optional<java.lang.String> channelName();
  public java.util.Optional<java.lang.String> claimDomain();
  public java.util.Optional<java.lang.String> messageKind();
  public java.util.Optional<java.lang.String> reason();
  public java.util.Optional<systems.zlink.framework.monitoring.ZLinkMeshNodeState> state();
}
public interface systems.zlink.framework.monitoring.ZLinkMonitoringOptions {
  public abstract void addSocketEvents(java.lang.String, systems.zlink.framework.monitoring.ZLinkSocketEventKind...);
  public abstract void addSpotEvents(java.lang.String, java.time.Duration);
  public abstract void addLocationRuntimeEvents(java.lang.String, java.time.Duration);
}
public final class systems.zlink.framework.monitoring.ZLinkRuntimeErrorEvent extends java.lang.Record implements systems.zlink.framework.monitoring.ZLinkRuntimeEvent {
  public systems.zlink.framework.monitoring.ZLinkRuntimeErrorEvent(java.lang.String, java.time.Instant, systems.zlink.framework.monitoring.ZLinkRuntimeErrorEventKind, java.lang.String, java.lang.String, java.util.Optional<java.lang.String>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String sourceName();
  public java.time.Instant timestamp();
  public systems.zlink.framework.monitoring.ZLinkRuntimeErrorEventKind event();
  public java.lang.String callbackName();
  public java.lang.String exceptionType();
  public java.util.Optional<java.lang.String> message();
}
public interface systems.zlink.framework.monitoring.ZLinkRuntimeEvent {
  public abstract java.lang.String sourceName();
  public abstract java.time.Instant timestamp();
}
public interface systems.zlink.framework.monitoring.ZLinkRuntimeEventHandler<TEvent extends systems.zlink.framework.monitoring.ZLinkRuntimeEvent> {
  public abstract void handle(TEvent);
}
public final class systems.zlink.framework.monitoring.ZLinkSocketDiagnostic extends java.lang.Record {
  public systems.zlink.framework.monitoring.ZLinkSocketDiagnostic(int, long);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public int nativeEvent();
  public long nativeValue();
}
public final class systems.zlink.framework.monitoring.ZLinkSocketEvent extends java.lang.Record implements systems.zlink.framework.monitoring.ZLinkRuntimeEvent {
  public systems.zlink.framework.monitoring.ZLinkSocketEvent(java.lang.String, java.time.Instant, systems.zlink.framework.monitoring.ZLinkSocketEventKind, java.util.Optional<systems.zlink.contracts.core.RoutingId>, java.lang.String, java.lang.String, java.util.Optional<systems.zlink.framework.monitoring.ZLinkSocketDiagnostic>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String sourceName();
  public java.time.Instant timestamp();
  public systems.zlink.framework.monitoring.ZLinkSocketEventKind event();
  public java.util.Optional<systems.zlink.contracts.core.RoutingId> routingId();
  public java.lang.String localAddr();
  public java.lang.String remoteAddr();
  public java.util.Optional<systems.zlink.framework.monitoring.ZLinkSocketDiagnostic> diagnostic();
}
public final class systems.zlink.framework.monitoring.ZLinkSocketEventKind extends java.lang.Enum<systems.zlink.framework.monitoring.ZLinkSocketEventKind> {
  public static final systems.zlink.framework.monitoring.ZLinkSocketEventKind CONNECTED;
  public static final systems.zlink.framework.monitoring.ZLinkSocketEventKind CONNECTION_READY;
  public static final systems.zlink.framework.monitoring.ZLinkSocketEventKind DISCONNECTED;
  public static final systems.zlink.framework.monitoring.ZLinkSocketEventKind HANDSHAKE_FAILED;
  public static final systems.zlink.framework.monitoring.ZLinkSocketEventKind PEER_ADMISSION_CHANGED;
  public static final systems.zlink.framework.monitoring.ZLinkSocketEventKind CLOSED;
  public static final systems.zlink.framework.monitoring.ZLinkSocketEventKind INTERNAL;
  public static systems.zlink.framework.monitoring.ZLinkSocketEventKind[] values();
  public static systems.zlink.framework.monitoring.ZLinkSocketEventKind valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.monitoring.ZLinkSpotEvent extends java.lang.Record implements systems.zlink.framework.monitoring.ZLinkRuntimeEvent {
  public systems.zlink.framework.monitoring.ZLinkSpotEvent(java.lang.String, java.time.Instant, systems.zlink.framework.monitoring.ZLinkSpotEventKind, java.util.Optional<systems.zlink.framework.monitoring.ZLinkMeshNodeState>, java.util.List<systems.zlink.framework.monitoring.ZLinkMeshPeerSnapshot>, java.util.List<java.lang.String>, java.util.Optional<java.lang.String>);
  public final java.lang.String toString();
  public final int hashCode();
  public final boolean equals(java.lang.Object);
  public java.lang.String sourceName();
  public java.time.Instant timestamp();
  public systems.zlink.framework.monitoring.ZLinkSpotEventKind event();
  public java.util.Optional<systems.zlink.framework.monitoring.ZLinkMeshNodeState> state();
  public java.util.List<systems.zlink.framework.monitoring.ZLinkMeshPeerSnapshot> peers();
  public java.util.List<java.lang.String> subjects();
  public java.util.Optional<java.lang.String> timerDiagnostic();
}
```

## Runtime event support public signature

```java
public final class systems.zlink.framework.monitoring.ZLinkFlowOrigin extends java.lang.Enum<systems.zlink.framework.monitoring.ZLinkFlowOrigin> {
  public static final systems.zlink.framework.monitoring.ZLinkFlowOrigin INBOUND;
  public static final systems.zlink.framework.monitoring.ZLinkFlowOrigin TIMER;
  public static final systems.zlink.framework.monitoring.ZLinkFlowOrigin APPLICATION;
  public static final systems.zlink.framework.monitoring.ZLinkFlowOrigin LIFECYCLE;
  public static systems.zlink.framework.monitoring.ZLinkFlowOrigin[] values();
  public static systems.zlink.framework.monitoring.ZLinkFlowOrigin valueOf(java.lang.String);
}
public final class systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind extends java.lang.Enum<systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind> {
  public static final systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind STATUS_CHANGED;
  public static final systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind TOPOLOGY_CHANGED;
  public static final systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind SERVICE_SUMMARY_CHANGED;
  public static final systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind STORE_FAILURE;
  public static final systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind STORE_RECOVERED;
  public static systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind[] values();
  public static systems.zlink.framework.monitoring.ZLinkLocationRuntimeEventKind valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.monitoring.ZLinkRuntimeErrorEventKind extends java.lang.Enum<systems.zlink.framework.monitoring.ZLinkRuntimeErrorEventKind> {
  public static final systems.zlink.framework.monitoring.ZLinkRuntimeErrorEventKind MESSAGE_FLOW_OBSERVER_FAILED;
  public static systems.zlink.framework.monitoring.ZLinkRuntimeErrorEventKind[] values();
  public static systems.zlink.framework.monitoring.ZLinkRuntimeErrorEventKind valueOf(java.lang.String);
  public int value();
}
public final class systems.zlink.framework.monitoring.ZLinkSpotEventKind extends java.lang.Enum<systems.zlink.framework.monitoring.ZLinkSpotEventKind> {
  public static final systems.zlink.framework.monitoring.ZLinkSpotEventKind STATUS_CHANGED;
  public static final systems.zlink.framework.monitoring.ZLinkSpotEventKind PEERS_CHANGED;
  public static final systems.zlink.framework.monitoring.ZLinkSpotEventKind SUBJECTS_CHANGED;
  public static final systems.zlink.framework.monitoring.ZLinkSpotEventKind TIMER_HANDLER_FAILED;
  public static final systems.zlink.framework.monitoring.ZLinkSpotEventKind TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION;
  public static systems.zlink.framework.monitoring.ZLinkSpotEventKind[] values();
  public static systems.zlink.framework.monitoring.ZLinkSpotEventKind valueOf(java.lang.String);
  public int value();
}
```
