[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Java Binding Specification

This document defines the public contract surface of the Java binding.
Every exported API class, its purpose, and all public method signatures are
listed. Internal helpers and implementation details are omitted.

When this document lists an API absent from the Java source, this document is
the implementation target. Java binding work must add that API or explicitly
change this specification before claiming core alignment.

Core binding types live in the `systems.zlink` package.
Service extension types live in `systems.zlink.service.registry`,
`systems.zlink.service.discovery`, and
`systems.zlink.service.spot`.
Codec and Netty adapter contracts are specified in separate extension
documents linked from the `Message` section.

Only the packages and types listed in this document are public contract.
Packages, classes, and methods not listed here are implementation detail,
even when Java visibility is broader for package wiring. If the binding uses
JPMS/module export control, only documented public packages may be exported.
Perf, samples, and tests must use the documented Java entrypoint only.

Notation:

- `@Nullable T` means the method may return `null` only for the no-data case
  described next to that API. Other failures still raise the documented
  exception type.
- `Optional<T>` means absence is part of the value contract and callers must
  handle it without relying on `null`.

## Design Basis

The Java binding follows the repository POSD design policy. Public classes
must hide native sequencing, ownership, and option encoding behind typed,
deep interfaces so callers do not need core implementation details.

The public Java surface must model stable domain concepts, not native downcall
steps. Public classes are justified when they own context/socket lifetime,
message ownership, receive metadata, service membership, callbacks, or typed
options. Panama/JNI handles, part-loop sequencing, request tokens, callback
userdata, and raw option encoding stay inside non-exported implementation
packages.

Design review uses these POSD constraints:

- shared send/recv, nonblocking, ownership, and exception mapping rules live
  in one internal owner rather than being copied across socket classes
- canonical result and facade methods do not ask callers to pass state already
  captured by the object, such as a source socket, request sequence, or
  service address
- compatibility aliases, if retained, are clearly outside the canonical API and
  are not used by new docs, samples, or tests
- a public class or method that only forwards to a native call without adding
  validation, ownership, lifetime, or result-shape semantics is too shallow and
  must be removed or moved to an internal package

## High-Performance Requirements

The Java binding is part of a high-performance messaging library. Hot paths
must not use reflection, dynamic method lookup, per-message classpath scanning,
unnecessary allocation, avoidable byte-buffer copies, coarse lock contention,
hidden waits, sleeps, busy waits, or thread joins. JNI/JNA bridge code must
materialize Java `Message` and result objects directly from the core `*_part`
substrate and must not create native aggregate arrays only to copy them back
into Java collections.

Reflection is not an acceptable implementation fallback for Java API alignment.
Missing public APIs must be implemented with typed facades over direct Panama
downcalls or with direct implementation bridges. Callback stub creation may
resolve a `MethodHandle` during setup, but message send/recv, request/reply,
dispatch, poller, and timer progress must not perform reflection or
reflective lookup in the processing loop. Implementation bridges are not
public contract and applications must not depend on them.

## Core Feature Coverage

This binding specification must account for every public capability declared
by `core/include/zlink.h`. A core capability may be mapped in one of four
ways:

- **Public API**: exposed through documented Java classes in this file.
- **Typed facade**: exposed through a narrower Java type instead of raw C
  option ids or raw part functions.
- **Internal primitive**: used only by the binding implementation and not part
  of the public Java contract.
- **Required Java API**: present in the core header and now defined by this
  document as the Java binding contract. If the current Java implementation
  does not expose it yet, the implementation must be updated to match this
  document before the binding can be considered aligned.

| Core capability | C entrypoints | Java contract mapping | Status |
|-----------------|---------------|-----------------------|--------|
| Runtime errors and version | `zlink_errno`, `zlink_strerror`, `zlink_version` | `Zlink.strerror`, `Zlink.version`, `ZlinkException.getInternalErrno` | Public API; raw `errno()` stays internal |
| Capabilities | `zlink_has` | `Zlink.has` | Public API |
| Context lifecycle | `zlink_ctx_new`, `zlink_ctx_shutdown`, `zlink_ctx_term` | `Context`, `Context.shutdown`, `Context.close` | Public API |
| Context options | `zlink_ctx_set`, `zlink_ctx_set_data`, `zlink_ctx_get` | `ContextOptions` | Typed facade |
| Context auto HWM recalculation | `zlink_ctx_auto_hwm_recalculate` | `Context.recalculateAutoHwm()` | Required Java API |
| Message ownership and copying | `zlink_msg_init`, `zlink_msg_init_size`, `zlink_msg_close`, `zlink_msg_move`, `zlink_msg_copy`, `zlink_msg_adopt`, `zlink_msg_data`, `zlink_msg_size`, `zlink_msg_refcnt`, `zlink_msg_gets` | `Message` constructors, `Message.copyOf`, `move`, `data`, `size`, `refCount`, `getProperty`, `close` | Public API / typed facade |
| Borrowed external message storage | `zlink_msg_init_data` with caller free hook | No public borrowed-wrap API; public adapters copy into owned `Message` | Internal primitive |
| Socket construction and lifecycle | `zlink_socket`, `zlink_close` | typed socket classes and `close` | Public API |
| Bind/connect lifecycle | `zlink_bind`, `zlink_connect`, `zlink_unbind`, `zlink_disconnect`, `zlink_disconnect_rid` | socket `bind`, `connect`, `unbind`, `disconnect`, `disconnectRid` on connectable raw sockets; `SpotNode` peer methods | Public API |
| Common socket options | `zlink_set_option`, `zlink_get_option`, `zlink_set_routing_id`, `zlink_get_routing_id` | typed socket option classes and routing-id accessors | Public API / typed facade |
| TLS helpers | `zlink_set_tls_server`, `zlink_set_tls_client` | `Socket.setTlsServer`, `Socket.setTlsClient`, service TLS methods | Public API |
| Router options | `zlink_set_router_option`, `zlink_get_router_option` | `RouterSocketOptions` | Typed facade |
| Dealer options | `zlink_set_dealer_option` | `DealerSocketOptions` | Typed facade; core has no dealer option getter |
| Pub/XPub options | `zlink_set_pub_option`, `zlink_get_pub_option` | `PubSocketOptions` | Typed facade |
| Sub/XSub options | `zlink_set_sub_option`, `zlink_get_sub_option`, `zlink_subscription_at` | `SubSocketOptions`, direct subscription methods | Typed facade |
| Stream options | `zlink_set_stream_option`, `zlink_get_stream_option` | `StreamSocketOptions` | Typed facade |
| SPOT and SpotNode options | `zlink_set_spot_option`, `zlink_get_spot_option`, `zlink_set_spot_node_option`, `zlink_get_spot_node_option` | direct `Spot` request-timeout methods and SpotNode option methods | Required Java API |
| Channel discovery attachment | `zlink_socket_attach_discovery`, `zlink_socket_set_channel_name`, `zlink_socket_get_channel_name` | `attachDiscovery`, `DealerSocket.setChannelName`, `DealerSocket.getChannelName` | Public API |
| Plain send/recv | `zlink_send_part`, `zlink_send_part_rid`, `zlink_recv_part`, `zlink_router_recv_part` | `send`, routed `send`, `recv`, `Received` | Public API; raw part loop stays internal |
| Pub/sub data plane | `zlink_publish_part`, `zlink_set_subscription`, `zlink_unset_subscription`, `zlink_subscribe_part`, `zlink_xpub_recv_part` | `publish`, `setSubscription`, `unsetSubscription`, `subscribe`, `receiveSubscriptionEvent` | Public API |
| Request/reply | `zlink_dealer_request_part`, `zlink_router_request_part`, `zlink_router_reply_part` | `DealerSocket.request`, `RouterSocket.request`, `RouterSocket.reply`, `Received.reply` | Public API |
| Router to Spot routing | `zlink_router_send_spot_part`, `zlink_router_request_spot_part`, `zlink_router_reply_spot_part` | `RouterSocket.sendToSpot`, `requestToSpot`, `replyToSpot` | Public API |
| Stream packet callbacks | `zlink_recv_handler` for raw `STREAM`, `zlink_stream_packet_handler`, `zlink_send_ready_handler` | `StreamSocket.onPacket`, raw direct stream callbacks stay internal, `onSendReady` | Public API / internal callback bridge |
| Stream actor binding | `zlink_stream_bind_actor`, `zlink_stream_unbind_actor`, `zlink_stream_send_bound_actor_part` | `StreamSocket.bindActor`, `unbindActor`, `sendBoundActor` | Public API |
| Socket monitoring | `zlink_socket_monitor_open`, `zlink_socket_monitor_handler`, `zlink_socket_monitor_recv`, `zlink_monitor_snapshot`, `zlink_monitor_close`, `zlink_monitor_ignore_handler` | `MonitorSocket`, `MonitorEvent`, `MonitorSnapshot`, `IGNORE_HANDLER` | Public API |
| Registry service | `zlink_registry_*` | `Registry`, `RegistryStatus`, service summary, member peer, topology entries | Public API |
| Discovery service | `zlink_discovery_*` | `Discovery`, `resolveSpot`, `resolveActor`, `memberPeers`, value and registry connection methods | Public API |
| Spot lifecycle | `zlink_spot_new`, `zlink_spot_destroy`, `zlink_spot_node_new`, `zlink_spot_node_destroy`, `zlink_spot_node_entry_spot`, `zlink_spot_node_spot_lookup` | `SpotNode`, `entrySpot`, `createSpot`, `spotLookup`, `close` | Required Java API |
| Spot node peer wiring | `zlink_spot_node_bind`, `zlink_spot_node_connect_peer`, `zlink_spot_node_disconnect_peer`, `zlink_spot_node_disconnect_peer_rid`, `zlink_spot_node_attach_discovery`, `zlink_spot_node_attach_channel_dealer`, `zlink_spot_node_attach_channel_dealer_manual`, `zlink_spot_node_attach_pub_ingress` | `SpotNode.bind`, `connectPeer`, `disconnectPeer`, `disconnectPeerRid`, attachment methods | Public API |
| Spot data plane | `zlink_spot_send_channel_part`, `zlink_spot_publish_part`, `zlink_spot_subscribe_part`, `zlink_spot_subscription_event_recv`, `zlink_spot_recv_part` | `Spot.sendChannel`, `publish`, `subscribe`, `receiveSubscriptionEvent`, `recvRouted` | Public API |
| Spot request/reply | `zlink_spot_request_channel_part`, `zlink_spot_request_spot_part`, `zlink_spot_request_router_part`, `zlink_spot_send_spot_part`, `zlink_spot_reply_spot_part`, `zlink_spot_reply_router_part`, `zlink_spot_channel_reply_progress_from` | `Spot.requestChannel`, `requestToSpot`, `requestToRouter`, `sendToSpot`, `replyToSpot`, `replyToRouter`; channel reply progress stays internal | Public API |
| Spot dispatch callbacks | `zlink_spot_handler`, `zlink_spot_dispatch_event_handler` | `Spot.onRoutedReceive`, `Spot.onDispatchEvent`, `SpotDispatchInfo` | Public API |
| Actor dispatch | `zlink_remote_actor_get_ref` (async), `zlink_spot_node_actor_new`, `zlink_spot_node_actor_lookup`, `zlink_spot_node_actor_destroy` (async), `zlink_spot_node_actor_join_spot` (async + dedicated completion), `zlink_spot_actor_join_recv`, `zlink_spot_actor_join_reply`, `zlink_spot_actor_lifecycle_handler`, `zlink_spot_node_actor_leave_spot` (async), `zlink_spot_node_actor_recv_part`, `zlink_spot_node_actor_send_bound_session_msg`, `zlink_spot_node_actor_close_bound_session`, `zlink_stream_bound_actors` | `Actor`, `ActorRef`, `ActorJoinResult`, `ActorLookupResult`, `SpotActorLifecycleInfo`, `SpotNode` actor methods, `Spot.recvActorJoin`, `Spot.replyActorJoin`, `Spot.onActorLifecycle`, `StreamSocket.boundActors` | Public API |
| Spot snapshots | `zlink_spot_node_status_snapshot`, `zlink_spot_node_peers_snapshot`, `zlink_spot_node_peers_query`, `zlink_spot_node_subjects_snapshot`, `zlink_spot_node_internal_sockets_snapshot`, `zlink_spot_node_spots_snapshot`, `zlink_spot_node_actors_snapshot`, `zlink_spot_actors_snapshot`, registry/discovery topology snapshot/query entrypoints | `statusSnapshot`, peer/subject/internal-socket/spot/actor snapshot methods, registry/discovery topology records | Public API |
| Polling | `zlink_poll`, `zlink_poller_*` | `Poller`, `PollEvent`, `PollEventFlag` | Public API; legacy array `zlink_poll` is intentionally not exposed |
| Poller timers | `zlink_poller_add_timer`, `zlink_poller_remove_timer` | `Poller.add(Timer, Object)`, `Poller.remove(Timer)`, `PollEvent.timer` | Required Java API |
| Proxy | `zlink_proxy`, `zlink_proxy_steerable` | `Zlink.proxy`, `Zlink.proxySteerable` | Public API |
| Timer | `zlink_timer_*`, `zlink_spot_timer_new` | `Timer`, `Timer.fromSpot`, `start`, `stop`, `recv`, `onFire`, `close` | Public API |
| Stopwatch | `zlink_stopwatch_*` | `Stopwatch` | Public API |
| Sleep and threads | `zlink_sleep`, `zlink_thread_start`, `zlink_thread_join` | `Zlink.sleep`, `ZlinkThread` | Public API |
| Atomic counter | `zlink_atomic_counter_*` | `AtomicCounter` | Public API |
| Multipart cleanup | `zlink_multipart_close` | `Message.closeAll` | Public API |

The C entrypoint column is traceability for coverage review only. It is not a
Java public signature list and does not expose helper substrate sequencing to
applications. Use the API sections below as the single source of Java public
signatures.

Java implementation alignment rule:

If an entry below is absent from the current Java source, that is an
implementation alignment issue, not a documentation exception. The Java
binding must expose the public API defined in this document, or this
specification must be changed first.

---

## Core Alignment Rules

The detailed sections below are the canonical Java binding contract. This
section states cross-cutting constraints once so the per-type API lists can
stay focused on signatures.

- `PairSocket`, `DealerSocket`, and `RouterSocket` keep their documented
  send, recv, request, and reply methods, but they do not expose direct
  data-plane receive callbacks such as `onReceive(...)`.
- `SubSocket` and `XSubSocket` are receive-only topic sockets and do not
  expose direct topic callbacks such as `onSubscribe(...)`.
- `StreamSocket` keeps `recv(...)` and exposes `onPacket(...)` as the public
  framed stream packet callback. The callback receives the source routing id,
  one owned header `Message`, and one owned body `Message`. Raw direct stream
  callbacks remain binding internals unless this specification is changed
  first.
- `SpotNode` must expose channel-aware attachment APIs:
  `attachDiscovery(Discovery discovery)`,
  `attachChannelDealer(Discovery discovery, DealerSocket dealer)`,
  `attachChannelDealerManual(String channelName, DealerSocket dealer)`, and
  `attachPubIngress(PubSocket pub)`.
- `Spot` must expose channel-aware data-plane operation builders:
  `sendChannel(...)`, `sendToSpot(...)`, `requestChannel(...)`, and
  `publish(String topic)`.
- `Spot.subscribe(...)` returns a `TopicMessage`.
  `TopicMessage` exposes topic, parts, and optional routing id.
- `Spot` must not expose `onSubscribe(...)`.
- `SpotDispatchEvent.SUBSCRIBE_READABLE` and `.ROUTED_READABLE` are readiness
  notifications, not one-event-per-message delivery counters. Binding docs and
  samples must drain until the recv path reports `EAGAIN`.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through
  typed option/property surfaces. The value range is `0..100`, default `100`;
  `0` drains new outbound selection. Submit attempts to a weight-`0` peer
  raise `SubmitException` with `getCode() == SubmitResult.NOT_ADMITTED`.
- `POLLOUT` is a send-recovery readiness signal, shared with
  `onSendReady(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `mandatory =
  true`, `handover = false`, `nodrop = true`.
- SPOT admission HWM and dispatch worker defaults follow the core header.
  Router and pubsub admission profile/numeric options are exposed; dispatch
  worker min/max options are exposed as callback worker pool sizing. Relay and
  delivery HWM stay `0` and are not public Java options.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routingId, advertiseEndpoint)`. Users do not configure this.

## Actor Dispatch Public Surface

Java exposes Actor dispatch through public classes in
`systems.zlink.service.spot` and related service packages.

`SpotNode` exposes `createActor`, `actorLookup`, `remoteActorGetRef` (async),
`destroyActor` (async), `joinActor` (async, dedicated completion), `leaveActor`
(async), `spotsSnapshot`, and `actorsSnapshot`. `Spot` exposes `recvActorJoin`,
`replyActorJoin`, `onActorLifecycle`, and `actorsSnapshot`. `StreamSocket`
exposes `bindActor` (async), `unbindActor` (async), `sendBoundActor`, and
`boundActors`. `Discovery` exposes `resolveActor`.

`generation == 0` is an unchecked remote ref. Actor readable dispatch uses
preloaded parts when Java moves native callbacks to managed executors.
The exact public signatures are defined once in the Actor section below.

## Core

### Context

RAII-style context that manages IO threads and sockets.
Implements `AutoCloseable`.

```java
public final class Context implements AutoCloseable {
    Context();

    ContextOptions options();
    void recalculateAutoHwm();                                      // @throws ConfigException
    void shutdown();                                                 // @throws CloseException
    void close();                                                    // @throws CloseException
}
```

## Peer Disconnect by Routing ID

Java bindings expose `disconnectRid(routingId)` on connectable raw sockets and
`disconnectPeerRid(targetNodeRid)` on `SpotNode`. `StreamSocket` is bind-only
and does not expose peer-rid disconnect. The duplicate policy option and
`NOT_FOUND` / `CONFLICT` / `BUSY` connect errors mirror the C core. `Spot` does
not expose a peer-rid disconnect method.

### ContextOptions

Typed facade for context configuration options.

```java
public final class ContextOptions {
    int ioThreads();                                                 // @throws ConfigException
    void ioThreads(int count);                                       // @throws ConfigException
    int maxSockets();                                                // @throws ConfigException
    void maxSockets(int count);                                      // @throws ConfigException
    int socketLimit();                                               // @throws ConfigException
    int threadPriority();                                            // @throws ConfigException
    void threadPriority(int priority);                               // @throws ConfigException
    int threadSchedulingPolicy();                                    // @throws ConfigException
    void threadSchedulingPolicy(int policy);                         // @throws ConfigException
    String threadNamePrefix();                                       // @throws ConfigException
    void threadNamePrefix(String prefix);                            // @throws ConfigException
    int maxMsgSize();                                                // @throws ConfigException
    void maxMsgSize(int bytes);                                      // @throws ConfigException
    int msgTSize();                                                  // @throws ConfigException
    boolean blocky();                                                // @throws ConfigException
    void blocky(boolean enabled);                                    // @throws ConfigException
    boolean autoHwmEnabled();                                        // @throws ConfigException
    void autoHwmEnabled(boolean enabled);                            // @throws ConfigException
    Duration autoHwmRecalcDebounce();                                // @throws ConfigException
    void autoHwmRecalcDebounce(Duration value);                       // @throws ConfigException
    AutoHwmProfile autoHwmProfile();                                  // @throws ConfigException
    void autoHwmProfile(AutoHwmProfile profile);                      // @throws ConfigException
    void addThreadAffinity(int cpu);                                 // @throws ConfigException
    void removeThreadAffinity(int cpu);                              // @throws ConfigException
}
```

Deprecated C compatibility no-ops such as the old auto-HWM total memory budget
option are not part of the canonical Java public API. Compatibility support, if
needed, must live outside this public contract.

---

## Socket Types

### Common Socket Surface

All socket types inherit from `Socket`, but the public base surface is limited
to lifecycle, monitoring, TLS, and common typed options. Capability-specific
operations such as `send`, `recv`, `publish`, `subscribe`, `onSendReady`, and
`attachDiscovery` are documented only on the concrete socket types that support
them.

```java
// Available on all socket types
void close();                                                    // @throws CloseException
MonitorSocket monitorOpen();                                    // @throws ConfigException
MonitorSocket monitorOpen(MonitorEventType... events);          // @throws ConfigException
void setTlsServer(String certPem, String keyPem,
                  boolean requireClientCert);                    // @throws ConfigException
void setTlsClient(String caCertPem, String hostname,
                  boolean trustSystem);                          // @throws ConfigException
CommonSocketOptions options();
// No common peer-weight accessor. Bindings expose peer weight only on
// RouterSocket and DealerSocket.
```

`SendOp.submit()` from `send().message(...)` and `publish(topic).message(...)`
returns `false` only for temporary backpressure when `SendFlags.DONT_WAIT` has
been configured via the builder's `.flags(...)` stage. Blocking submit returns
`true` on success. Route-not-ready and other submit failures still raise
`SubmitException`.
Blocking no-argument receive methods do not return `null`. Nullable overloads
that accept `RecvFlags` return `null` when `RecvFlags.DONT_WAIT` finds no data.
Timer `recv(...)` reports no data through `RecvException` with
`RecvResult.NO_DATA`. All receive paths still raise `RecvException` for real
recv failures.

After `attachDiscovery(...)` succeeds on a `DealerSocket`, `RouterSocket`,
`PubSocket`, or `SubSocket`, the attached socket lifecycle is owned by the
`Discovery` instance. Direct `connect(...)`, `disconnect(...)`,
`disconnectRid(...)`, `unbind(...)`, and `close()` calls on that socket fail
through the documented native error mapping; `Discovery.close()` closes
attached participants.

### SendReadyHandler

Callback interface for socket send-recovery readiness.

```java
@FunctionalInterface
public interface SendReadyHandler {
    void onReady();
}
```

`onReady()` is a readiness notification shared with `POLLOUT`. It means a
previous nonblocking send path may be retried; it is not a guarantee that every
transport peer is writable.

All handler registration methods reject `null` handlers. Passing `null` is not
a detach operation; registered callbacks are released only by closing the
owning socket, monitor, timer, or service object.

### CommonSocketOptions

Typed facade for common `zlink_option_t` values that are safe to expose on
socket option objects. Socket-specific option classes extend this facade.

```java
public class CommonSocketOptions {
    Duration linger();                                              // @throws ConfigException
    void linger(Duration value);                                    // @throws ConfigException
    int sendHwm();                                                  // @throws ConfigException
    void sendHwm(int value);                                        // @throws ConfigException
    int recvHwm();                                                  // @throws ConfigException
    void recvHwm(int value);                                        // @throws ConfigException
    Duration sendTimeout();                                         // @throws ConfigException
    void sendTimeout(Duration value);                               // @throws ConfigException
    Duration recvTimeout();                                         // @throws ConfigException
    void recvTimeout(Duration value);                               // @throws ConfigException
    boolean immediate();                                            // @throws ConfigException
    void immediate(boolean enabled);                                // @throws ConfigException
    Duration connectTimeout();                                      // @throws ConfigException
    void connectTimeout(Duration value);                            // @throws ConfigException
    boolean ipv6();                                                 // @throws ConfigException
    void ipv6(boolean enabled);                                     // @throws ConfigException
    boolean tcpNoDelay();                                           // @throws ConfigException
    void tcpNoDelay(boolean enabled);                               // @throws ConfigException
    int tcpKeepalive();                                             // @throws ConfigException
    void tcpKeepalive(int value);                                   // @throws ConfigException
    Duration heartbeatInterval();                                   // @throws ConfigException
    void heartbeatInterval(Duration value);                         // @throws ConfigException
    Duration heartbeatTtl();                                        // @throws ConfigException
    void heartbeatTtl(Duration value);                              // @throws ConfigException
    Duration heartbeatTimeout();                                    // @throws ConfigException
    void heartbeatTimeout(Duration value);                          // @throws ConfigException
    RidDuplicatePolicy ridDuplicatePolicy();                        // @throws ConfigException
    void ridDuplicatePolicy(RidDuplicatePolicy value);              // @throws ConfigException
    long maxMsgSize();                                              // @throws ConfigException
    void maxMsgSize(long value);                                    // @throws ConfigException
    int autoHwmMessageUnitBytes();                                  // @throws ConfigException
    void autoHwmMessageUnitBytes(int value);                        // @throws ConfigException
    int backlog();                                                  // @throws ConfigException
    void backlog(int value);                                        // @throws ConfigException
    Duration reconnectInterval();                                   // @throws ConfigException
    void reconnectInterval(Duration value);                         // @throws ConfigException
    Duration reconnectIntervalMax();                                // @throws ConfigException
    void reconnectIntervalMax(Duration value);                      // @throws ConfigException
    String lastEndpoint();                                          // @throws ConfigException
}
```

TLS certificate, key, CA, hostname, verification, client-certificate,
trust-system, and password options are exposed through `Socket.setTlsServer`
and `Socket.setTlsClient` instead of individual raw option methods.

### PairSocket

Bidirectional exclusive pair socket.

```java
public final class PairSocket extends Socket {
    PairSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void disconnectRid(RoutingId routingId);                         // @throws ConnectException

    // --- send (operation builder) ---
    SendOp send();

    @Deprecated Received recv();                                     // @throws RecvException (legacy: returns fresh Received per call)
    @Deprecated @Nullable Received recv(RecvFlags flags);            // @throws RecvException (legacy)
    boolean recv(Received result, RecvFlags flags);                  // canonical caller-provided storage @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException
}
```

### PubSocket

Publisher socket. Sends topic-prefixed messages to all matching subscribers.

```java
public final class PubSocket extends Socket {
    PubSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void disconnectRid(RoutingId routingId);                         // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException

    // --- publish (operation builder) ---
    SendOp publish(String topicId);
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    PubSocketOptions options();
}
```

### PubSocketOptions

```java
public class PubSocketOptions extends CommonSocketOptions {
    boolean verbose();                                               // @throws ConfigException
    void verbose(boolean enabled);                                  // @throws ConfigException
    boolean verboser();                                              // @throws ConfigException
    void verboser(boolean enabled);                                 // @throws ConfigException
    boolean noDrop();                                                // @throws ConfigException
    void noDrop(boolean enabled);                                   // @throws ConfigException
    boolean manual();                                                // @throws ConfigException
    void manual(boolean enabled);                                   // @throws ConfigException
    boolean manualLastValue();                                       // @throws ConfigException
    void manualLastValue(boolean enabled);                          // @throws ConfigException
    void approveSubscribe(RoutingId routingId);                     // @throws ConfigException
    void rejectSubscribe(RoutingId routingId);                      // @throws ConfigException
    Message welcomeMessage();                                        // @throws ConfigException
    void welcomeMessage(Message message);                           // @throws ConfigException
    int topicsCount();                                               // @throws ConfigException
}
```

### SubSocket

Subscriber socket. Receives topic-filtered messages from publishers.

```java
public final class SubSocket extends Socket {
    SubSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void disconnectRid(RoutingId routingId);                         // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException

    void setSubscription(String filter);                             // @throws ConfigException
    void unsetSubscription(String filter);                           // @throws ConfigException
    Optional<SubscriptionEntry> subscriptionAt(int index);           // @throws ConfigException
    TopicMessage subscribe();                                        // @throws RecvException
    @Nullable TopicMessage subscribe(RecvFlags flags);               // @throws RecvException

    SubSocketOptions options();
}
```

### SubSocketOptions

```java
public final class SubSocketOptions extends CommonSocketOptions {
    int topicsCount();                                               // @throws ConfigException
}
```

### DealerSocket

Asynchronous client socket for fair-queued request distribution.

```java
public final class DealerSocket extends Socket {
    DealerSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void disconnectRid(RoutingId routingId);                         // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException
    void setChannelName(String channelName);                         // @throws ConfigException
    String getChannelName();                                         // @throws ConfigException

    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    // --- send (operation builder) ---
    SendOp send();

    @Deprecated Received recv();                                     // @throws RecvException (legacy: returns fresh Received per call)
    @Deprecated @Nullable Received recv(RecvFlags flags);            // @throws RecvException (legacy)
    boolean recv(Received result, RecvFlags flags);                  // canonical caller-provided storage @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    // --- request (operation builder) ---
    RequestOp request();

    DealerSocketOptions options();
}
```

### DealerSocketOptions

```java
public final class DealerSocketOptions extends CommonSocketOptions {
    void probe(boolean enabled);                                    // @throws ConfigException
    void requestTimeout(Duration value);                            // @throws ConfigException
    void peerWeight(int value);                                     // @throws ConfigException
}
```

The C core exposes `zlink_set_dealer_option` but no
`zlink_get_dealer_option`, so dealer-specific options are setter-only unless
the core adds a getter.

### RouterSocket

Server socket that routes messages to specific peers by routing id.

```java
public final class RouterSocket extends Socket {
    RouterSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void disconnectRid(RoutingId routingId);                         // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException

    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    // --- routed send (operation builder) ---
    SendOp send(RoutingId rid);

    @Deprecated Received recv();                                     // @throws RecvException (legacy: returns fresh Received per call)
    @Deprecated @Nullable Received recv(RecvFlags flags);            // @throws RecvException (legacy)
    boolean recv(Received result, RecvFlags flags);                  // canonical caller-provided storage @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    // --- request to a specific peer (operation builder) ---
    RequestOp request(RoutingId rid);

    // --- reply to a received request (operation builder) ---
    ReplyOp reply(RoutingId rid, long requestSeq);

    // --- router -> spot routed send (operation builder) ---
    SendOp sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);

    // --- router -> spot routed request (operation builder) ---
    RequestOp requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);

    // --- router -> spot routed reply (operation builder) ---
    ReplyOp replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                        long requestSeq);

    RouterSocketOptions options();
}
```

### RouterSocketOptions

```java
public final class RouterSocketOptions extends CommonSocketOptions {
    boolean mandatory();                                             // @throws ConfigException
    void mandatory(boolean enabled);                                // @throws ConfigException
    boolean handover();                                              // @throws ConfigException
    void handover(boolean enabled);                                  // @throws ConfigException
    boolean probe();                                                 // @throws ConfigException
    void probe(boolean enabled);                                    // @throws ConfigException
    Optional<RoutingId> connectRoutingId();                         // @throws ConfigException
    void connectRoutingId(RoutingId routingId);                     // @throws ConfigException
    Duration requestTimeout();                                      // @throws ConfigException
    void requestTimeout(Duration value);                            // @throws ConfigException
    int peerWeight();                                                // @throws ConfigException
    void peerWeight(int value);                                     // @throws ConfigException
}
```

### XPubSocket

Extended publisher. Like PubSocket but also receives subscription events.

```java
public final class XPubSocket extends Socket {
    XPubSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void disconnectRid(RoutingId routingId);                         // @throws ConnectException

    // --- publish (operation builder) ---
    SendOp publish(String topicId);

    SubscriptionEvent receiveSubscriptionEvent();                    // @throws RecvException
    @Nullable SubscriptionEvent receiveSubscriptionEvent(RecvFlags flags); // @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    PubSocketOptions options();
}
```

### XSubSocket

Extended subscriber. Like SubSocket with raw subscription forwarding.

```java
public final class XSubSocket extends Socket {
    XSubSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void connect(String endpoint);                                   // @throws ConnectException
    void unbind(String endpoint);                                    // @throws ConnectException
    void disconnect(String endpoint);                                // @throws ConnectException
    void disconnectRid(RoutingId routingId);                         // @throws ConnectException

    void setSubscription(String filter);                             // @throws ConfigException
    void unsetSubscription(String filter);                           // @throws ConfigException
    Optional<SubscriptionEntry> subscriptionAt(int index);           // @throws ConfigException
    TopicMessage subscribe();                                        // @throws RecvException
    @Nullable TopicMessage subscribe(RecvFlags flags);               // @throws RecvException

    SubSocketOptions options();
}
```

### StreamSocket

Raw TCP stream socket. Bind-only; does not support `connect`, `disconnect`,
or `disconnectRid`.

```java
public final class StreamSocket extends Socket {
    StreamSocket(Context ctx);

    void bind(String endpoint);                                      // @throws BindException
    void unbind(String endpoint);                                    // @throws ConnectException

    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    // --- routed send (operation builder) ---
    SendOp send(RoutingId rid);

    @Deprecated Received recv();                                     // @throws RecvException (legacy: returns fresh Received per call)
    @Deprecated @Nullable Received recv(RecvFlags flags);            // @throws RecvException (legacy)
    boolean recv(Received result, RecvFlags flags);                  // canonical caller-provided storage @throws RecvException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException

    // Two mutually-exclusive receive modes on the same StreamSocket:
    //   (1) recv(), (2) onPacket(handler). Second attach raises
    //   HandlerException(HandlerResult.BUSY).
    // onPacket(handler) is the public framed stream packet callback.
    //   Wire frame is big-endian u16 header_size + u32 body_size
    //   + header + body. The handler receives the source routing id,
    //   one owned header Message, and one owned body Message.
    //   Raw direct stream callbacks are implementation detail.
    void onPacket(StreamPacketHandler handler);                      // @throws HandlerException

    // --- Actor bind/unbind (operation builders) ---
    // The stream is bound to its session-owner SpotNode at attach time; no
    // SpotNode argument is passed here. A bind does not require nor imply a
    // Spot join.
    ActorBindOp bindActor(RoutingId sessionRid, ActorRef actor);
    ActorUnbindOp unbindActor(RoutingId sessionRid, String actorId);
    // --- session-bound relay send (operation builder) ---
    SendOp sendBoundActor(RoutingId sessionRid, String actorId);
    // Snapshot of Actor refs attached to the given session (local mapping only).
    List<ActorRef> boundActors(RoutingId sessionRid);                 // @throws ConfigException

    StreamSocketOptions options();
}
```

### StreamPacketHandler

Callback interface for `StreamSocket.onPacket(...)`.

```java
@FunctionalInterface
public interface StreamPacketHandler {
    void onPacket(RoutingId routingId, Message header, Message body);
}
```

The `header` and `body` messages are owned by the callback. The callback must
consume or close both messages before returning. Runtime exceptions thrown by
the callback are reported through the binding's callback failure path.

### StreamSocketOptions

```java
public final class StreamSocketOptions extends CommonSocketOptions {
    boolean notifyEnabled();                                         // @throws ConfigException
    void notify(boolean enabled);                                   // @throws ConfigException
}
```

---

## Message / Domain Types

### Message

Owns one native message frame. Implements `AutoCloseable`.

```java
public final class Message implements AutoCloseable {
    Message();                                                       // @throws ConfigException
    Message(int size);                                               // @throws ConfigException

    // --- factories (copy) ---
    // All public input adapters copy into an owned zlink message.
    // Public borrowed external-wrap APIs are intentionally not exposed.
    static Message copyOf(Message source);                          // @throws ConfigException
    static Message copyOf(byte[] data);                              // @throws ConfigException
    static Message copyOf(byte[] data, int offset, int length);      // @throws ConfigException
    static Message copyOfUtf8(String value);                         // @throws ConfigException
    static Message copyOf(ByteBuffer data);                          // @throws ConfigException
    static Message copyOf(ByteSpan span);                            // @throws ConfigException
    static Message copyOf(MemorySegment data);                       // @throws ConfigException
    static Message copyOf(MemorySegment data, long offset, long length); // @throws ConfigException

    // --- accessors ---
    int size();
    int refCount();
    boolean empty();
    boolean valid();
    byte[] data();
    byte[] toByteArray();
    String toUtf8String();
    ByteBuffer dataBuffer();
    int readIntLe(int offset);
    long readLongLe(int offset);
    String getProperty(String key);                                  // @throws ConfigException
    Message move();                                                  // @throws ConfigException

    // --- writable owned payload ---
    void fill(byte value);                                           // @throws ConfigException
    void fill(byte value, int offset, int length);                   // @throws ConfigException
    void writeByte(int offset, byte value);                          // @throws ConfigException
    void writeIntLe(int offset, int value);                          // @throws ConfigException
    void writeLongLe(int offset, long value);                        // @throws ConfigException

    // --- copy to destination ---
    int copyTo(byte[] destination);
    int copyTo(byte[] destination, int offset);
    int copyTo(ByteBuffer destination);
    boolean tryCopyTo(ByteBuffer destination);

    // --- batch close ---
    static void closeAll(Message[] parts);
    static void closeAll(Iterable<? extends Message> parts);

    void close();                                                    // @throws CloseException
}
```

`Message` core contract stays on JDK-owned buffer types such as `byte[]`,
`ByteBuffer`, and `MemorySegment`. Third-party network buffer types such as
Netty `ByteBuf` are kept out of the core artifact so the base Java binding
does not take a mandatory Netty dependency.

### ByteSpan

Lightweight byte-span view used by copy-oriented message factories and
low-copy adapters.

```java
public interface ByteSpan {
    MemorySegment segment();
    int length();
    ByteBuffer asByteBuffer();

    static ByteSpan of(byte[] data);
    static ByteSpan of(byte[] data, int offset, int length);
    static ByteSpan of(ByteBuffer data);
    static ByteSpan of(MemorySegment data);

    record SegmentBackedSpan(MemorySegment segment, int length) implements ByteSpan {}
}
```

`ByteSpan` does not transfer ownership of the backing memory. APIs that accept
`ByteSpan` as public input copy the bytes into an owned `Message` unless the
method explicitly documents a different lifetime rule.

### Boundary Validation

Java validates values that can overflow, truncate, or break native string
contracts before calling core. These validation failures use Java-native
exceptions such as `IllegalArgumentException`, `IndexOutOfBoundsException`, or
`NullPointerException`, not `ZlinkException`.

- `RoutingId` rejects empty input and values longer than 255 bytes at value
  object creation time.
- Actor ids must be non-empty UTF-8 strings, must not contain NUL, and must be
  at most 255 bytes.
- Endpoint strings passed to bind/connect-style APIs must be at most 255 bytes.
- `channelName` values passed to SPOT channel-aware APIs must be at most
  255 bytes.
- Topic and subscription filter strings must not contain NUL. Their length
  limit is left to core.
- `Duration` conversions to native milliseconds or nanoseconds must fail before
  truncation or overflow.
- Byte array, `ByteBuffer`, `MemorySegment`, and `ByteSpan` offset/length
  ranges are checked before native calls.

### Codec Extensions

Codec adapters are separate public extension artifacts layered on top of the
core binding. Their contract lives in [Java Codec Extension Specification](codec.md).
The core module does not expose codec entrypoints from
`systems.zlink`.

### Netty Buffer Extension

Netty `ByteBuf` support is a separate public extension. Its contract lives in
[Java Netty Extension Specification](netty.md). Netty-specific entrypoints are
not part of the core `systems.zlink` package.

The Netty extension is distributed as Maven `zlink-ext-netty` and exposes
`systems.zlink.netty`.

### RoutingId

Immutable binary-safe routing identity value object (1-255 bytes). The
canonical constructor is `fromBytes(byte[])`; no string-only constructor
is provided. `toHex()` is offered as a convenience only.

```java
public final class RoutingId {
    static final int MAX_LENGTH = 255;

    // --- factories (binary-safe) ---
    static RoutingId fromBytes(byte[] bytes);
    static RoutingId fromBytes(byte[] bytes, int offset, int length);
    static RoutingId fromString(String value); // parses toHex(); max 510 hex chars; invalid or >255 decoded bytes throws IllegalArgumentException

    // --- accessors ---
    byte[] toBytes();                       // defensive copy of raw bytes
    int size();                             // 1-255

    String toHex();

    boolean equals(Object other);
    int hashCode();
}
```

### SendFlags

Flags that control send behavior (blocking vs. non-blocking).

```java
public enum SendFlags {
    NONE(0),
    DONT_WAIT(1);

    private final int value;
    SendFlags(int value) { this.value = value; }
    public int value() { return value; }
}
```

### RecvFlags

Flags that control receive behavior (blocking vs. non-blocking).

```java
public enum RecvFlags {
    NONE(0),
    DONT_WAIT(1);

    private final int value;
    RecvFlags(int value) { this.value = value; }
    public int value() { return value; }
}
```

### Core Enum Values

Java enum values that mirror public core enums must keep the numeric value
from `core/include/zlink_enum.h`. Option-id enums are intentionally not public;
they are exposed through typed option methods above.

```java
public enum AutoHwmProfile {
    COMPACT(0),
    LOW_LATENCY(1),
    BALANCED(2),
    THROUGHPUT(3)
}

public enum SocketType {
    ANY(0),
    PAIR(0x1001),
    PUB(0x1002),
    SUB(0x1003),
    DEALER(0x1004),
    ROUTER(0x1005),
    XPUB(0x1006),
    XSUB(0x1007),
    STREAM(0x1008)
}

public enum RidDuplicatePolicy {
    REJECT(0),
    HANDOVER(1)
}

public enum MonitorEventType {
    CONNECTED(0x0001),
    CONNECT_DELAYED(0x0002),
    CONNECT_RETRIED(0x0004),
    LISTENING(0x0008),
    BIND_FAILED(0x0010),
    ACCEPTED(0x0020),
    ACCEPT_FAILED(0x0040),
    CLOSED(0x0080),
    CLOSE_FAILED(0x0100),
    DISCONNECTED(0x0200),
    MONITOR_STOPPED(0x0400),
    HANDSHAKE_FAILED_NO_DETAIL(0x0800),
    CONNECTION_READY(0x1000),
    HANDSHAKE_FAILED_PROTOCOL(0x2000),
    HANDSHAKE_FAILED_AUTH(0x4000),
    PEER_WEIGHT_CHANGED(0x8000),
    ALL(0xFFFF)
}

public enum MonitorSourceKind {
    SOCKET(1),
    SPOT_PUB(3),
    SPOT_SUB(4)
}

public enum AutoHwmRecalcReason {
    NONE(0),
    INITIAL(1),
    ROLE_CHANGE(2),
    POLICY_TOGGLE(3),
    REFRESH(4),
    DEFERRED_SHRINK(5)
}

public enum PollEventFlag {
    POLLIN(1),
    POLLOUT(2),
    POLLERR(4),
    POLLPRI(8)
}

public enum AutoConnectType {
    INVALID(0),
    ROUTE_MESH(1),
    CLIENT_SERVER(2),
    DEALER_MESH(3),
    FANOUT(4),
    SPOT_MESH(5)
}

public enum ServiceRole {
    INVALID(0),
    SPOT(2),
    ROUTER(3),
    DEALER(4),
    PUB(5),
    SUB(6)
}

public enum SpotRole {
    PUB(1),
    SUB(2)
}

public enum ServiceKind {
    DISCOVERY(1),
    SPOT_SUB(3),
    SPOT_PUB(4),
    SOCKET(5)
}

public enum ServiceEventSubjectKind {
    NONE(0),
    TOPIC(1),
    PATTERN(2)
}

public enum SpotNodeMode {
    PUBSUB(1),
    ROUTED(2),
    ALL(3)
}

public enum SpotNodeSocketOwner {
    ANY(0),
    NODE(1),
    SPOT(2)
}

public enum SpotNodeState {
    IDLE(1),
    CONNECTING(2),
    PARTIAL_READY(3),
    READY(4),
    ERROR(5)
}

public enum SpotPeerSource {
    MANUAL(1),
    DISCOVERY(2),
    MIXED(3)
}

public enum SpotPeerState {
    CONFIGURED(1),
    CONNECTING(2),
    CONNECTED(3)
}

public enum RegistryState {
    IDLE(1),
    ACTIVE(2),
    DEGRADED(3),
    ERROR(4)
}

public enum TopologySource {
    MANUAL(1),
    DISCOVERY(2),
    REGISTRY(3)
}

public enum TopologyState {
    DISCOVERED(1),
    CONNECTING(2),
    READY(3),
    LOST(4),
    ERROR(5),
    STOPPED(6)
}

```

`AutoHwmRecalcReason` maps the `MonitorSnapshot.autoHwmLastRecalcReason`
field.

### ZlinkException

Abstract unchecked parent of all zlink exceptions.
Every failing operation throws one of the eight concrete subclasses below,
each of which corresponds to a C API function-category result enum
(`SubmitException`, `RequestException`, `RecvException`, `HandlerException`,
`CloseException`, `BindException`, `ConnectException`, `ConfigException`).
All subclasses extend `RuntimeException` indirectly via `ZlinkException`, so
they are unchecked; callers do not need `throws` clauses. Catch
`ZlinkException` for the "catch-all" idiom, or a specific subclass when
finer-grained handling is required.

The `code` field is a globally unique `int` that spans all result enum
ranges (0-706). The code alone identifies the error without needing to
know which enum it belongs to. `internalErrno` carries the OS-level
errno when available (0 otherwise).

```java
public abstract class ZlinkException extends RuntimeException {
    protected ZlinkException(int code);
    protected ZlinkException(int code, int internalErrno);

    public int getCode();
    public int getInternalErrno();
    public String getMessage();
}
```

### SubmitException

Thrown by send / publish / reply / request (callback submit) operations.
Wraps a `SubmitResult`.

```java
public final class SubmitException extends ZlinkException {
    public SubmitException(SubmitResult result);
    public SubmitException(SubmitResult result, int internalErrno);
    public SubmitResult getResult();
}
```

### RequestException

Thrown by request completion paths (coroutine/future variants) and used as
the category for request-specific failures. Wraps a `RequestResult`.
Callback-style `request(...)` methods deliver `RequestResult` directly to
the callback rather than throwing this exception.

```java
public final class RequestException extends ZlinkException {
    public RequestException(RequestResult result);
    public RequestException(RequestResult result, int internalErrno);
    public RequestResult getResult();
}
```

### RecvException

Thrown by recv / subscribe / subscription-event / monitor recv / timer recv
operations. Wraps a `RecvResult`.

```java
public final class RecvException extends ZlinkException {
    public RecvException(RecvResult result);
    public RecvException(RecvResult result, int internalErrno);
    public RecvResult getResult();
}
```

### HandlerException

Thrown by handler registration methods (`onPacket`, `onSendReady`,
`onRoutedReceive`, `onDispatchEvent`, `onEvent`, etc.). Wraps a
`HandlerResult`.

```java
public final class HandlerException extends ZlinkException {
    public HandlerException(HandlerResult result);
    public HandlerException(HandlerResult result, int internalErrno);
    public HandlerResult getResult();
}
```

### CloseException

Thrown by `close()` / `destroy()` operations. Wraps a `CloseResult`.

```java
public final class CloseException extends ZlinkException {
    public CloseException(CloseResult result);
    public CloseException(CloseResult result, int internalErrno);
    public CloseResult getResult();
}
```

### BindException

Thrown by `bind(...)` operations. Wraps a `BindResult`.

```java
public final class BindException extends ZlinkException {
    public BindException(BindResult result);
    public BindException(BindResult result, int internalErrno);
    public BindResult getResult();
}
```

### ConnectException

Thrown by `connect(...)`, `disconnect(...)`, and `unbind(...)` operations.
Wraps a `ConnectResult`.

```java
public final class ConnectException extends ZlinkException {
    public ConnectException(ConnectResult result);
    public ConnectException(ConnectResult result, int internalErrno);
    public ConnectResult getResult();
}
```

### ConfigException

Thrown by option set/get, snapshot, poller mutation, proxy, timer
configuration, TLS setup, discovery attach, and message lifecycle
operations. Wraps a `ConfigResult`.

```java
public final class ConfigException extends ZlinkException {
    public ConfigException(ConfigResult result);
    public ConfigException(ConfigResult result, int internalErrno);
    public ConfigResult getResult();
}
```

### SubmitResult

Result code for send/request/reply/publish operations.
Maps 1-to-1 to the C API `zlink_submit_result_t`.

```java
public enum SubmitResult {
    OK(0),
    BACKPRESSURED(1),
    NOT_CONNECTED(2),
    NOT_FOUND(3),
    TERMINATED(4),
    INVALID_HANDLE(5),
    INVALID_ARGUMENT(6),
    NOT_SUPPORTED(7),
    INVALID_STATE(8),
    THREAD_VIOLATION(9),
    OUT_OF_MEMORY(10),
    SEQ_EXHAUSTED(11),
    INTERNAL_ERROR(12),
    NOT_ADMITTED(13);  // target peer has weight 0

    SubmitResult(int value);
    public int value();
    public static SubmitResult fromValue(int value);
}
```

### RequestResult

Result code delivered to request completion callbacks and futures.

```java
public enum RequestResult {
    OK(0),
    TIMED_OUT(101),
    NOT_FOUND(102),
    TERMINATED(103),
    PROTOCOL_ERROR(104),
    INTERNAL_ERROR(105),
    REJECTED(106),
    CONFLICT(107),
    BUSY(108),
    NOT_CONNECTED(109),
    INVALID_ARGUMENT(110),
    INVALID_STATE(111),
    NOT_SUPPORTED(112);

    RequestResult(int value);
    public int value();
    public static RequestResult fromValue(int value);
}
```

### RequestCallback

Callback interface used by callback-submit request methods.

```java
@FunctionalInterface
public interface RequestCallback {
    void onComplete(RequestResult result, List<Message> parts);
}
```

The `parts` list passed to `RequestCallback` is unmodifiable. Message ownership
still follows the normal `Message` lifecycle rules. When `result !=
RequestResult.OK`, `parts` is an empty list.

### RecvResult

Result code for recv, subscribe, and subscription event operations.

```java
public enum RecvResult {
    OK(0),
    NO_DATA(201),
    BUSY(202),
    TERMINATED(203),
    INVALID_HANDLE(204),
    NOT_SUPPORTED(205),
    INTERNAL_ERROR(206);

    RecvResult(int value);
    public int value();
    public static RecvResult fromValue(int value);
}
```

### HandlerResult

Result code for handler registration operations (`onPacket`,
`onSendReady`, `onRoutedReceive`, `onDispatchEvent`, `onEvent`, etc.).

```java
public enum HandlerResult {
    OK(0),
    INVALID_ARGUMENT(301),
    BUSY(302),
    NOT_SUPPORTED(303),
    DEADLOCK(304),
    INVALID_HANDLE(305),
    INTERNAL_ERROR(306);

    HandlerResult(int value);
    public int value();
    public static HandlerResult fromValue(int value);
}
```

### CloseResult

Result code for close and destroy operations.

```java
public enum CloseResult {
    OK(0),
    BUSY(401),
    SHUTDOWN(402),
    INVALID_HANDLE(403),
    INTERNAL_ERROR(404);

    CloseResult(int value);
    public int value();
    public static CloseResult fromValue(int value);
}
```

### BindResult

Result code for bind operations.

```java
public enum BindResult {
    OK(0),
    INVALID_ARGUMENT(501),
    ADDR_IN_USE(502),
    NOT_SUPPORTED(503),
    INVALID_HANDLE(504),
    INTERNAL_ERROR(505);

    BindResult(int value);
    public int value();
    public static BindResult fromValue(int value);
}
```

### ConnectResult

Result code for connect, disconnect, and unbind operations.

```java
public enum ConnectResult {
    OK(0),
    INVALID_ARGUMENT(601),
    NOT_SUPPORTED(602),
    INVALID_HANDLE(603),
    INTERNAL_ERROR(604),
    NOT_FOUND(605),
    CONFLICT(606),
    BUSY(607);

    ConnectResult(int value);
    public int value();
    public static ConnectResult fromValue(int value);
}
```

### ConfigResult

Result code for configuration, option, and snapshot operations.

```java
public enum ConfigResult {
    OK(0),
    INVALID_HANDLE(701),
    INVALID_ARGUMENT(702),
    NOT_SUPPORTED(703),
    INTERNAL_ERROR(704),
    INVALID_STATE(705),
    NOT_FOUND(706);

    ConfigResult(int value);
    public int value();
    public static ConfigResult fromValue(int value);
}
```

### Received

Aggregates one recv result with optional routing id, optional request
sequence, and message parts. Implements `AutoCloseable`.

```java
public final class Received implements AutoCloseable {
    Optional<RoutingId> routingId();               // peer_rid (Router) / source_node_rid (Spot)
    Optional<RoutingId> spotRid();                 // present only for SPOT routed recv
    Optional<Long> requestSeq();                   // present for request messages only
    List<Message> parts();
    boolean isSinglePart();
	    Message firstPart();                                             // @throws RecvException
	    Message singlePartOrThrow();                                     // @throws RecvException

    // Send a regular routed message back to the sender of this Received.
    // Source rid / spot rid are encapsulated; the builder accumulates payload
    // via .message(...).
    SendOp send();

    // Reply to this received request. Valid only when requestSeq is present.
    // Routing id, spot id, and request seq are encapsulated.
    ReplyOp reply();

    void close();                                                    // @throws CloseException
}
```

`Received` owns the reply context needed to answer the original sender.
`reply(...)` encapsulates routing id, optional spot id, and request sequence
so callers do not need to rebuild the route. Calling `reply(...)` when
`requestSeq()` is empty, when the reply context is invalid, or after the source
socket is closed raises `SubmitException`. A closed source socket maps to the
terminated submit result.

### TopicMessage

Topic-aware recv result used by SUB and Spot subscribe paths.
Implements `AutoCloseable`.

```java
public final class TopicMessage implements AutoCloseable {
    Optional<RoutingId> routingId();
    String topic();                          // UTF-8
    List<Message> parts();
    boolean isSinglePart();
    Message firstPart();                                             // @throws RecvException
    Message singlePartOrThrow();                                     // @throws RecvException

    void close();                                                    // @throws CloseException
}
```

`TopicMessage` instances are produced by `subscribe(...)` receive paths. Public
constructors are not part of the contract because the binding owns the native
message parts and service/topic decoding rules. `firstPart()` raises
`RecvException` when the message has no parts. `singlePartOrThrow()` raises
`RecvException` unless the message has exactly one part.

### SubscriptionEvent

Reports a subscribe/unsubscribe event from XPub sockets and Spot
subscription event recv. Immutable value object; no lifecycle methods.

```java
public record SubscriptionEvent(Optional<RoutingId> routingId,
                                String topic,           // UTF-8
                                boolean subscribed) {}
```

### SubscriptionEntry

Snapshot entry returned by `SubSocket.subscriptionAt`, `XSubSocket.subscriptionAt`,
and `Spot.subscriptionAt`.

```java
public record SubscriptionEntry(String filter, boolean pattern) {
    byte[] filterBytes();
}
```

---

## Monitoring

### MonitorSocket

Socket-level event monitor. Receives connect, disconnect, and handshake events.
Implements `AutoCloseable`.
Starts in recv model. `onEvent(...)` transitions one-way to callback-only
model; after that `recv()` raises busy and `snapshot()` still works.

```java
public final class MonitorSocket implements AutoCloseable {
    /** No-op callback for callback-only model. Pass to
     *  {@link #onEvent(SocketMonitorHandler)} to keep a valid handler symbol
     *  when the application does not care about events; once installed, the
     *  monitor is in callback-only model and {@link #recv()} raises busy
     *  ({@link #snapshot()} still works). To drive the monitor through
     *  {@code snapshot()} / {@code recv()} instead, leave the handler unset.
     *  Maps to zlink_monitor_ignore_handler. */
    public static final SocketMonitorHandler IGNORE_HANDLER = event -> {};

    void onEvent(SocketMonitorHandler handler);                      // @throws HandlerException
    MonitorEvent recv();                                             // @throws RecvException
    @Nullable MonitorEvent recv(RecvFlags flags);                    // @throws RecvException
    MonitorSnapshot snapshot();                                      // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### SocketMonitorHandler

Callback interface for `MonitorSocket.onEvent(...)`.

```java
@FunctionalInterface
public interface SocketMonitorHandler {
    void onEvent(MonitorEvent event);
}
```

`onEvent(...)` receives the same `MonitorEvent` shape as `MonitorSocket.recv()`.
Installing a handler moves the monitor into callback-only mode.

### MonitorEvent

Socket monitor event value object. Produced by `MonitorSocket.recv()`.

```java
public record MonitorEvent(MonitorEventType event,
                           long value,
                           Optional<RoutingId> routingId,
                           String localAddr,
                           String remoteAddr) {}
```

`MonitorEventType` includes `PEER_WEIGHT_CHANGED` (bit 15). When this
event fires, `value` carries the new `0..100` weight for the peer.

### MonitorSnapshot

Runtime state snapshot produced by `MonitorSocket.snapshot()`. Immutable
value object.

```java
public record MonitorSnapshot(MonitorSourceKind sourceKind,
                              int stateFlags,
                              int detailFlags,
                              long sndPendingMsgs,
                              long rcvPendingMsgs,
                              boolean autoHwmEnabled,
                              AutoHwmProfile autoHwmProfile,
                              int autoHwmRole,
                              int autoHwmPolicyClass,
                              long autoHwmUnitBudgetBytes,
                              int autoHwmSizeCap,
                              long autoHwmSocketMessageSlots,
                              long autoHwmEffectiveMessageBytes,
                              int autoHwmAppliedSndHwm,
                              int autoHwmAppliedRcvHwm,
                              int autoHwmAppliedSndBuffer,
                              int autoHwmAppliedRcvBuffer,
                              long autoHwmLastRecalcMs,
                              AutoHwmRecalcReason autoHwmLastRecalcReason,
                              int autoHwmSendBlockedRatioPpm,
                              int autoHwmDeferredSndHwm,
                              int autoHwmDeferredRcvHwm) {
    // Ready is meaningful only for raw socket monitor sources.
    boolean isReady();
}
```

---

## Services

### Registry

Registry service node. Manages service topology and membership broadcast.
Implements `AutoCloseable`.

```java
public final class Registry implements AutoCloseable {
    Registry(Context ctx);

    void bind(String pubEndpoint, String routerEndpoint);            // @throws BindException
    void setId(int id);                                              // @throws ConfigException
    void addPeer(String peerPubEndpoint);                            // @throws ConfigException
    void setHeartbeat(Duration interval, Duration timeout);          // @throws ConfigException
    void setBroadcastInterval(Duration interval);                    // @throws ConfigException
    void setTlsServer(String certPem, String keyPem, boolean requireClientCert); // @throws ConfigException
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem);   // @throws ConfigException

    RegistryStatus statusSnapshot();                                 // @throws ConfigException
    List<RegistryServiceSummaryEntry> serviceSummarySnapshot();      // @throws ConfigException
    List<RegistryServiceSummaryEntry> serviceSummarySnapshot(
        RegistryServiceSummaryFilter filter);                        // @throws ConfigException
    List<RegistryTopologyEntry> topologySnapshot();                  // @throws ConfigException
    List<RegistryTopologyEntry> topologyQuery(RegistryTopologyFilter filter); // @throws ConfigException
    List<MemberPeerEntry> memberPeers(String channelName); // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### Discovery

Fixed-channel discovery view. Tracks one auto-connect type and channel name.
Implements `AutoCloseable`.

```java
public final class Discovery implements AutoCloseable {
    Discovery(Context ctx, AutoConnectType autoConnectType, String channelName);

    void connectRegistry(String registryEndpoint);                   // @throws ConnectException
    void setValue(long value);                                       // @throws ConfigException
    long getValue();                                                 // @throws ConfigException
    void setSpotOwnerSyncEnabled(boolean enabled);                   // @throws ConfigException
    boolean isSpotOwnerSyncEnabled();                                // @throws ConfigException
    void setActorRouteSyncEnabled(boolean enabled);                  // @throws ConfigException
    boolean isActorRouteSyncEnabled();                               // @throws ConfigException
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem); // @throws ConfigException

    List<MemberPeerEntry> memberPeers();                             // @throws ConfigException

    RoutingId resolveSpot(RoutingId spotRid);                        // @throws ConfigException
    ActorRoute resolveActor(String actorId);                         // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### SpotNode

Spot node lifecycle and topology facade.
Implements `AutoCloseable`.

```java
public final class SpotNode implements AutoCloseable {
    SpotNode(Context ctx);
    SpotNode(Context ctx, SpotNodeMode mode);

    void bind(String endpoint);                                      // @throws BindException
    void connectPeer(String peerEndpoint);                           // @throws ConnectException
    void disconnectPeer(String peerEndpoint);                        // @throws ConnectException
    void disconnectPeerRid(RoutingId targetNodeRid);                 // @throws ConnectException
    void attachDiscovery(Discovery discovery);                       // @throws ConfigException
    void attachChannelDealer(Discovery discovery, DealerSocket dealer); // @throws ConfigException
    void attachChannelDealerManual(String channelName, DealerSocket dealer); // @throws ConfigException
    void attachPubIngress(PubSocket pub);                            // @throws ConfigException
    void setTlsServer(String certPem, String keyPem, boolean requireClientCert); // @throws ConfigException
    void setTlsClient(String caCertPem, String hostname, boolean trustSystem);   // @throws ConfigException

    // --- identity / routing ---
    // Logical address used for routed ownership.
    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException

    // --- Spot factories owned by the node ---
    Spot entrySpot();                                                // @throws ConfigException
    Spot createSpot();                                               // @throws ConfigException
    Optional<Spot> spotLookup(RoutingId spotRid);                    // @throws ConfigException

    // --- node option facade ---
    AutoHwmProfile routerHwmProfile();                               // @throws ConfigException
    void routerHwmProfile(AutoHwmProfile profile);                   // @throws ConfigException
    int routerHwm();                                                 // @throws ConfigException
    void routerHwm(int value);                                       // @throws ConfigException
    AutoHwmProfile pubsubHwmProfile();                               // @throws ConfigException
    void pubsubHwmProfile(AutoHwmProfile profile);                   // @throws ConfigException
    int pubsubHwm();                                                 // @throws ConfigException
    void pubsubHwm(int value);                                       // @throws ConfigException
    int dispatchWorkersMin();                                        // @throws ConfigException
    void dispatchWorkersMin(int value);                              // @throws ConfigException
    int dispatchWorkersMax();                                        // @throws ConfigException
    void dispatchWorkersMax(int value);                              // @throws ConfigException

    // --- actor dispatch (operation builders) ---
    Actor createActor(String actorId);                               // @throws ConfigException
    ActorRef actorLookup(String actorId);                            // @throws ConfigException
    // Async remote Actor lookup. Completion delivers ActorLookupResult
    // (checked ref on success).
    ActorLookupOp remoteActorGetRef(RoutingId targetNodeRid, String actorId);
    // Async destroy. Succeeds only when the Actor is in the Entry Spot.
    ActorDestroyOp destroyActor(ActorRef actor);
    // Async user-Spot join. Completion delivers ActorJoinResult (final
    // Actor ref, joined Spot rid, join_epoch) plus reply parts.
    // destSpotRid must be a user Spot (Entry Spot is not a valid target).
    // A bound STREAM session is NOT required to join a user Spot.
    // Multipart join state payload accumulates through .message(...).
    ActorJoinOp joinActor(ActorRef actor, RoutingId destNodeRid,
                          RoutingId destSpotRid);
    // Async leave to the same node's Entry Spot.
    ActorLeaveOp leaveActor(ActorRef actor, RoutingId currentSpotRid);
    // Actor-to-session relay (operation builder). Fire-and-forget reverse
    // send through the bound STREAM session.
    SendOp sendBoundSessionMsg(ActorRef actor);

    SpotNodeStatus statusSnapshot();                                 // @throws ConfigException
    List<SpotNodePeerEntry> peersSnapshot();                         // @throws ConfigException
    List<SpotNodePeerEntry> peersQuery(SpotNodePeerFilter filter);   // @throws ConfigException
    List<SpotNodeSubjectEntry> subjectsSnapshot();                   // @throws ConfigException
    List<SpotNodeSubjectEntry> subjectsSnapshot(SpotNodeSubjectFilter filter); // @throws ConfigException
    List<SpotNodeSocketSnapshotEntry> internalSocketsSnapshot();      // @throws ConfigException
    List<SpotNodeSocketSnapshotEntry> internalSocketsSnapshot(
        SpotNodeSocketSnapshotFilter filter);                        // @throws ConfigException
    List<SpotNodeSpotEntry> spotsSnapshot();                         // @throws ConfigException
    List<SpotNodeActorEntry> actorsSnapshot();                       // @throws ConfigException
    // close() first closes every live Spot owned by this node.
    void close();                                                    // @throws CloseException
}
```

`SpotNode` owns `Spot` lifecycles. Public `Spot` instances are created through
`entrySpot()`, `createSpot()`, or `spotLookup(...)`. Direct `Spot` constructors
are not public contract.

`attachPubIngress(pub)` attaches an external raw `PUB` source that feeds the
node's SPOT topic plane. It is distinct from the internal publish ingress path
used by `Spot.publish(...)`; applications must not rely on internal queue,
socket, or thread names to reason about publish delivery.

`dispatchWorkersMin()` / `dispatchWorkersMax()` configure only the
`SpotNode` callback worker pool used by dispatch callbacks. They do not change
the core I/O thread count or the data-plane thread count. Values follow the
core validation rule: `min >= 1` and `max >= min`.

The `SpotNode(Context, SpotNodeMode)` constructor selects the native node
shape. If `mode` is `null`, the binding uses `SpotNodeMode.ALL`. This keeps the
default constructor and the explicit mode constructor equivalent unless the
caller chooses a narrower node mode.

### Spot

Spot messaging endpoint. Provides SPOT topic pub/sub and routed messaging.
Implements `AutoCloseable`. Public instances are created by `SpotNode`.

```java
public final class Spot implements AutoCloseable {
    // --- identity / routing ---
    // Logical address and routed ownership key.
    void setRoutingId(RoutingId rid);                                // @throws ConfigException
    RoutingId routingId();                                           // @throws ConfigException
    Duration requestTimeout();                                      // @throws ConfigException
    void requestTimeout(Duration value);                            // @throws ConfigException

    // --- SPOT topic publish / channel-aware send / request operation builders ---
    SendOp publish(String topicId);
    SendOp sendChannel(String channelName);
    SendOp sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOp requestChannel(String channelName);

    // --- subscribe ---
    void setSubscription(String topicId);                            // @throws ConfigException
    void unsetSubscription(String topicIdOrPattern);                 // @throws ConfigException
    Optional<SubscriptionEntry> subscriptionAt(int index);            // @throws ConfigException
    void onSendReady(SendReadyHandler handler);                      // @throws HandlerException
    TopicMessage subscribe();                                        // @throws RecvException
    @Nullable TopicMessage subscribe(RecvFlags flags);               // @throws RecvException
    SubscriptionEvent receiveSubscriptionEvent();                    // @throws RecvException
    @Nullable SubscriptionEvent receiveSubscriptionEvent(RecvFlags flags); // @throws RecvException

    // --- routed request / reply operation builders ---
    RequestOp requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid);
    RequestOp requestToRouter(RoutingId peerRid);
    ReplyOp replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                        long requestSeq);
    ReplyOp replyToRouter(RoutingId peerRid, long requestSeq);

    // --- routed receive ---
    Received recvRouted();                                           // @throws RecvException
    @Nullable Received recvRouted(RecvFlags flags);                  // @throws RecvException
    void onRoutedReceive(SpotRoutedHandler handler);                 // @throws HandlerException
    void onDispatchEvent(SpotDispatchEventHandler handler);          // @throws HandlerException

    // --- actor dispatch ---
    ActorJoinRequest recvActorJoin();                                // @throws RecvException
    @Nullable ActorJoinRequest recvActorJoin(RecvFlags flags);       // @throws RecvException
    // Reply to an Actor join admission request (operation builder).
    // Multipart reply payload accumulates through .message(...);
    // a zero-message submit is allowed.
    ActorJoinReplyOp replyActorJoin(ActorJoinRequest request, boolean accepted);
    // Register Actor lifecycle callbacks for this Spot. Passing null for
    // both removes the registration. Handler receives SpotActorLifecycleInfo.
    void onActorLifecycle(@Nullable ActorLifecycleHandler onJoin,
                          @Nullable ActorLifecycleHandler onLeave);   // @throws HandlerException
    List<ActorRef> actorsSnapshot();                                 // @throws ConfigException

    void close();                                                    // @throws CloseException
}

public interface SendOp {
    SendSubmitOp message(Message part);
}

public interface SendSubmitOp {
    SendSubmitOp message(Message part);
    SendSubmitOp flags(SendFlags flags);
    boolean submit();                                                // @throws SubmitException
}

public interface RequestOp {
    RequestSubmitOp message(Message part);
}

public interface RequestSubmitOp {
    RequestSubmitOp message(Message part);
    RequestSubmitOp timeout(Duration timeout);
    RequestCallbackSubmitOp flags(SendFlags flags);
    CompletableFuture<List<Message>> submitAsync();                  // @throws SubmitException; future completes with RequestException on failure
    boolean submit(RequestCallback callback);                        // @throws SubmitException; callback receives RequestResult
}

public interface RequestCallbackSubmitOp {
    RequestCallbackSubmitOp message(Message part);
    RequestCallbackSubmitOp timeout(Duration timeout);
    RequestCallbackSubmitOp flags(SendFlags flags);
    boolean submit(RequestCallback callback);                        // @throws SubmitException; false only on temporary backpressure
}

public interface ReplyOp {
    ReplySubmitOp message(Message part);
}

public interface ReplySubmitOp {
    ReplySubmitOp message(Message part);
    ReplySubmitOp flags(SendFlags flags);
    void submit();                                                   // @throws SubmitException
}

// --- Actor operation builders ---

// SpotNode.joinActor(...) / Actor.join(spot) returns ActorJoinOp.
// Multipart join state payload is mandatory; the staged shape moves to
// ActorJoinSubmitOp only after the first message(...).
public interface ActorJoinOp {
    ActorJoinSubmitOp message(Message part);
}

public interface ActorJoinSubmitOp {
    ActorJoinSubmitOp message(Message part);
    ActorJoinSubmitOp timeout(Duration timeout);
    ActorJoinCallbackSubmitOp flags(SendFlags flags);
    CompletableFuture<ActorJoinCompletion> submitAsync();             // @throws SubmitException; future completes with RequestException on failure
    boolean submit(ActorJoinHandler callback);                        // @throws SubmitException
}

public interface ActorJoinCallbackSubmitOp {
    ActorJoinCallbackSubmitOp message(Message part);
    ActorJoinCallbackSubmitOp timeout(Duration timeout);
    ActorJoinCallbackSubmitOp flags(SendFlags flags);
    boolean submit(ActorJoinHandler callback);                        // @throws SubmitException
}

// Reply builder for Spot.replyActorJoin(request, accepted).
// Multipart reply payload is optional; submit() is visible directly.
public interface ActorJoinReplyOp {
    ActorJoinReplyOp message(Message part);
    void submit();                                                    // @throws SubmitException
}

// Payload-less Actor operation builders: leave, destroy, lookup, bind, unbind.
// All four expose optional timeout(...), then either submitAsync() or
// submit(callback). They never require a message(...) call.
public interface ActorLeaveOp {
    ActorLeaveOp timeout(Duration timeout);
    CompletableFuture<List<Message>> submitAsync();                   // @throws SubmitException
    boolean submit(ReplyHandler callback);                            // @throws SubmitException
}

public interface ActorDestroyOp {
    ActorDestroyOp timeout(Duration timeout);
    CompletableFuture<List<Message>> submitAsync();                   // @throws SubmitException
    boolean submit(ReplyHandler callback);                            // @throws SubmitException
}

public interface ActorLookupOp {
    ActorLookupOp timeout(Duration timeout);
    CompletableFuture<ActorLookupResult> submitAsync();               // @throws SubmitException
    boolean submit(ActorLookupHandler callback);                      // @throws SubmitException
}

public interface ActorBindOp {
    ActorBindOp timeout(Duration timeout);
    CompletableFuture<List<Message>> submitAsync();                   // @throws SubmitException
    boolean submit(ReplyHandler callback);                            // @throws SubmitException
}

public interface ActorUnbindOp {
    ActorUnbindOp timeout(Duration timeout);
    CompletableFuture<List<Message>> submitAsync();                   // @throws SubmitException
    boolean submit(ReplyHandler callback);                            // @throws SubmitException
}
```

`SendOp`, `RequestOp`, and `ReplyOp` are staged builders. A caller must add at
least one `message(...)` before any submit method is visible. Repeated
`message(...)` calls append multipart payload parts in order, so neither `Spot`
nor raw socket types expose separate single-message and `List<Message>`
overloads for send/request/reply paths. `RequestSubmitOp.submitAsync()` is the
async request form and does not accept submit flags. Calling `.flags(...)`
moves the request operation to the callback-submit stage. Submit consumes the
operation; using the same operation object again after submit must fail with a
validation error.

`ActorJoinOp` follows the same payload-mandatory shape as `RequestOp`.
`ActorJoinReplyOp` follows the `ReplyOp` shape but accepts a 0-part reply.
`ActorLeaveOp`, `ActorDestroyOp`, `ActorLookupOp`, `ActorBindOp`, and
`ActorUnbindOp` are payload-less builders — they support `timeout(...)` and
either `submitAsync()` or `submit(callback)`.

`onDispatchEvent` delivers `SpotDispatchInfo`. `CHANNEL_REPLY_READABLE`
dispatches are readiness notifications for internal request-progress work.
Request futures and callbacks progress their replies inside the binding; the
public API does not expose the native DEALER subject.
For `SUBSCRIBE_READABLE` and `ROUTED_READABLE`, callers must keep draining
`subscribe(...)` / `recvRouted(...)` until the binding surfaces no data /
`EAGAIN`.

### SpotRoutedHandler

Callback interface for `Spot.onRoutedReceive(...)`.

```java
@FunctionalInterface
public interface SpotRoutedHandler {
    void onMessage(Received received);
}
```

`received` owns the routed message parts and any live reply context. The
callback must consume or close it before returning.

### SpotDispatchEventHandler

Callback interface for `Spot.onDispatchEvent(...)`.

```java
@FunctionalInterface
public interface SpotDispatchEventHandler {
    void onEvent(SpotDispatchInfo info);
}
```

The handler receives readiness metadata only. Message delivery still happens
through the documented drain methods such as `subscribe(...)` and
`recvRouted(...)`.

### Actor

Actor object owned by a `SpotNode` and identified by `ActorRef`.

```java
public final class Actor implements AutoCloseable {
    ActorRef ref();                                                  // @throws ConfigException
    // Async user-Spot join (operation builder). Completion delivers
    // ActorJoinResult plus reply parts. spot must be a user Spot.
    // A bound STREAM session is NOT required.
    ActorJoinOp join(Spot spot);
    // Async leave to the same node's Entry Spot (operation builder).
    ActorLeaveOp leave(Spot spot);
    ActorPart recvPart();                                            // @throws RecvException
    @Nullable ActorPart recvPart(RecvFlags flags);                   // @throws RecvException
    // Actor-to-session relay (operation builder).
    SendOp sendBoundSession();
    void closeBoundSession();                                        // @throws RequestException
    void closeBoundSession(Duration timeout);                        // @throws RequestException
    void close();                                                    // @throws RequestException
    void close(Duration timeout);                                    // @throws RequestException
}
```

`sendBoundSession(...)` and `closeBoundSession(...)` use the Actor's current
bound STREAM session. Callers that need to select a specific session routing id
use `StreamSocket.sendBoundActor(...)`, which selects by session routing id and
actor id.

Actor value objects:

```java
public record ActorRef(RoutingId nodeRid, String actorId, long generation) {
}

public record ActorRoute(ActorRef actor,
                         boolean joined,
                         Optional<RoutingId> joinedSpotRid) {}

public record ActorRecvInfo(ActorRef actor,
                            RoutingId sourceNodeRid,
                            RoutingId sourceSessionRid,
                            int flags) {}

public record ActorPart(ActorRecvInfo info,
                        Message message,
                        boolean hasMore) implements AutoCloseable {
    void close();                                                    // @throws CloseException
}

public final class ActorJoinInfo {
    ActorRef sourceActor();
    ActorRef targetActor();
    RoutingId sourceNodeRid();
    RoutingId sourceSpotRid();
    RoutingId targetNodeRid();
    RoutingId targetSpotRid();
    long joinEpoch();
    int flags();
}

public final class ActorJoinRequest implements AutoCloseable {
    ActorJoinInfo info();
    Message message();
    void close();
}

public record ActorJoinResult(RequestResult result,
                              ActorRef actor,
                              RoutingId joinedSpotRid,
                              long joinEpoch,
                              int flags) {}

// Returned by the Future-style joinActor(...) variant to carry both the
// ActorJoinResult and the reply payload (parts).
public record ActorJoinCompletion(ActorJoinResult result,
                                  List<Message> replyParts) {}

public record ActorLookupResult(RequestResult result,
                                ActorRef actor,
                                int flags) {}

public record SpotActorLifecycleInfo(
        ActorRef previousActor,
        ActorRef currentActor,
        Optional<RoutingId> previousSpotRid,
        Optional<RoutingId> currentSpotRid,
        long joinEpoch,
        int flags) {}

@FunctionalInterface
public interface ActorJoinHandler {
    void onJoinResult(ActorJoinResult result, List<Message> replyParts);
}

@FunctionalInterface
public interface ActorLookupHandler {
    void onLookupResult(ActorLookupResult result);
}

@FunctionalInterface
public interface ActorLifecycleHandler {
    void onLifecycleEvent(Spot spot, SpotActorLifecycleInfo info);
}
```

`ActorPart` owns its message and closes that message from `close()`.
`SpotActorLifecycleInfo` is valid only for the duration of the
`ActorLifecycleHandler` callback; applications must copy fields they need to
retain.

### RegistryQueryClient

Remote registry query client. Connects to a registry and fetches topology snapshots.
Implements `AutoCloseable`.

```java
public final class RegistryQueryClient implements AutoCloseable {
    RegistryQueryClient(Context ctx);

    void connect(String endpoint);                                   // @throws ConnectException
    List<RegistryTopologyEntry> snapshot();                          // @throws ConfigException
    List<RegistryTopologyEntry> snapshot(RegistryTopologyFilter filter); // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

Primary entry types used in the default service flow:

### MemberPeerEntry

Discovery / Registry member peer entry value object.

```java
public record MemberPeerEntry(AutoConnectType autoConnectType,
                              ServiceRole serviceRole,
                              String channelName,
                              String endpoint,
                              RoutingId routingId,
                              long value,
                              int weight) {}
```

### RegistryTopologyEntry

Registry topology entry value object. Returned by
`Registry.topologySnapshot` / `topologyQuery` and
`RegistryQueryClient.snapshot`.

```java
public record RegistryTopologyEntry(AutoConnectType autoConnectType,
                                    RoutingId routingId,
                                    ServiceKind serviceKind,
                                    ServiceRole serviceRole,
                                    String channelName,
                                    String endpoint,
                                    TopologySource source,
                                    TopologyState state,
                                    int desiredCount,
                                    int readyCount,
                                    int errorCode,
                                    long lastReportedMs) {}
```

### SpotNodeStatus

Spot node status snapshot returned by `SpotNode.statusSnapshot`.

```java
public record SpotNodeStatus(String channelName,
                             String localEndpoint,
                             RoutingId nodeRoutingId,
                             SpotNodeState state,
                             int configuredPeerCount,
                             int activePeerCount,
                             int connectedPeerCount,
                             int subjectCount,
                             int readySubjectCount,
                             int disconnectedSubTargetCount,
                             int disconnectedRoutedTargetCount,
                             int lastError,
                             long lastChangedMs) {}
```

### SpotDispatchEvent

```java
public enum SpotDispatchEvent {
    SUBSCRIBE_READABLE,
    ROUTED_READABLE,
    TIMER_READABLE,
    CHANNEL_REPLY_READABLE,
    ACTOR_READABLE,
    ACTOR_JOIN_READABLE
}
```

### SpotDispatchSubjectKind

```java
public enum SpotDispatchSubjectKind {
    SPOT,
    TIMER,
    CHANNEL_DEALER,
    ACTOR
}
```

### SpotDispatchInfo

```java
public final class SpotDispatchInfo {
    SpotDispatchEvent event();
    SpotDispatchSubjectKind subjectKind();
    Optional<Timer> timer();
    List<ActorPart> actorParts();
}
```

`SpotDispatchInfo` does not expose the native dispatch subject. Channel reply
progress is driven inside the binding so request futures and callbacks can
complete without a public drain helper.

Advanced / Diagnostic entry types and filters:

### RegistryServiceSummaryEntry

Registry service summary entry value object. Returned by
`Registry.serviceSummarySnapshot`.

```java
public record RegistryServiceSummaryEntry(AutoConnectType autoConnectType,
                                          ServiceRole serviceRole,
                                          String channelName,
                                          int totalCount,
                                          int connectingCount,
                                          int readyCount,
                                          int errorCount,
                                          int stoppedCount,
                                          long lastReportedMs) {}
```

### RegistryStatus

Registry status snapshot returned by `Registry.statusSnapshot`.

```java
public record RegistryStatus(int registryId,
                             String bindEndpoint,
                             RegistryState state,
                             int topologyEntryCount,
                             int peerRegistryCount,
                             int connectedPeerRegistryCount,
                             long listSeq,
                             int lastError,
                             long lastChangedMs) {}
```

### SpotNodePeerEntry

Spot node peer entry value object. Returned by
`SpotNode.peersSnapshot` / `peersQuery`.

```java
public record SpotNodePeerEntry(String channelName,
                                String localEndpoint,
                                String peerEndpoint,
                                SpotPeerSource source,
                                SpotPeerState state,
                                int weight,
                                long connectedSinceMs,
                                long lastChangedMs) {}
```

### SpotNodeSubjectEntry

Spot node subject entry value object. Returned by
`SpotNode.subjectsSnapshot`.

```java
public record SpotNodeSubjectEntry(SpotRole role,
                                   String subject,
                                   ServiceEventSubjectKind subjectKind,
                                   int readyPeerCount,
                                   int activePeerCount,
                                   long lastChangedMs) {}
```

### SpotNodeSocketSnapshotEntry

Diagnostic socket snapshot entry returned by
`SpotNode.internalSocketsSnapshot(...)`. The snapshot exposes monitor-level
diagnostics, not mutable implementation handles.

```java
public record SpotNodeSocketSnapshotEntry(
    SpotNodeSocketOwner owner,
    long ownerId,
    String ownerName,
    String socketName,
    SocketType socketType,
    boolean autoHwmVisible,
    MonitorSnapshot snapshot) {}
```

### SpotNodeSpotEntry

Spot entry returned by `SpotNode.spotsSnapshot`.

```java
public record SpotNodeSpotEntry(RoutingId spotRid,
                                boolean dispatchHandlerAttached,
                                int joinedActorCount,
                                int pendingActorJoinCount,
                                boolean routeSynced,
                                long lastChangedMs) {}
```

### SpotNodeActorEntry

Actor route entry returned by `SpotNode.actorsSnapshot`.

```java
public record SpotNodeActorEntry(ActorRef actor,
                                 boolean joined,
                                 Optional<RoutingId> joinedSpotRid,
                                 boolean routeSynced,
                                 int pendingMessageCount,
                                 long lastChangedMs) {}
```

### SpotNodePeerFilter

Filter for `SpotNode.peersQuery`.

```java
public record SpotNodePeerFilter(String peerEndpoint,
                                 SpotPeerSource source,
                                 SpotPeerState state) {}
```

### SpotNodeSubjectFilter

Filter for `SpotNode.subjectsSnapshot`.

```java
public record SpotNodeSubjectFilter(SpotRole role,
                                    String subject,
                                    ServiceEventSubjectKind subjectKind) {}
```

### SpotNodeSocketSnapshotFilter

Filter for diagnostic socket snapshots returned by
`SpotNode.internalSocketsSnapshot(...)`.

```java
public record SpotNodeSocketSnapshotFilter(
    SpotNodeSocketOwner owner,
    SocketType socketType,
    String socketName) {}
```

### RegistryServiceSummaryFilter

Filter for `Registry.serviceSummarySnapshot`.

```java
public record RegistryServiceSummaryFilter(AutoConnectType autoConnectType,
                                           ServiceRole serviceRole,
                                           String channelName) {}
```

### RegistryTopologyFilter

Filter for `Registry.topologyQuery` and `RegistryQueryClient.snapshot`.

```java
public record RegistryTopologyFilter(AutoConnectType autoConnectType,
                                     ServiceKind serviceKind,
                                     ServiceRole serviceRole,
                                     String channelName,
                                     RoutingId routingId,
                                     TopologyState state,
                                     TopologySource source) {}
```

---

## Poller

### Poller

Event poller for multiplexing socket, file descriptor, and timer readiness.

The public poller contract is generic. It reports socket, file descriptor,
and timer readiness. SPOT dispatch ownership and drain semantics stay behind
`Spot.onDispatchEvent(...)`.
Implements `AutoCloseable`.

```java
public final class Poller implements AutoCloseable {
    Poller();

    // --- socket registration ---
    void add(Socket socket, PollEventFlag... events);                // @throws ConfigException
    void add(Socket socket, Object tag, PollEventFlag... events);    // @throws ConfigException
    void modify(Socket socket, PollEventFlag... events);             // @throws ConfigException
    boolean remove(Socket socket);                                   // @throws ConfigException

    // --- file descriptor registration ---
    void addFd(int fd, PollEventFlag... events);                     // @throws ConfigException
    void addFd(int fd, Object tag, PollEventFlag... events);         // @throws ConfigException
    void modifyFd(int fd, PollEventFlag... events);                  // @throws ConfigException
    boolean removeFd(int fd);                                        // @throws ConfigException

    // --- timer registration ---
    void add(Timer timer);                                           // @throws ConfigException
    void add(Timer timer, Object tag);                               // @throws ConfigException
    boolean remove(Timer timer);                                     // @throws ConfigException

    // --- poll ---
    int size();                                                      // @throws ConfigException
    @Nullable PollEvent wait(Duration timeout);                      // @throws RecvException
    List<PollEvent> wait(int maxEvents, Duration timeout);            // @throws RecvException
    int wait(List<PollEvent> destination, Duration timeout);          // @throws RecvException

    void clear();                                                    // @throws ConfigException
    void close();                                                    // @throws CloseException
}
```

### PollEvent

Immutable readiness result returned by `Poller.wait(...)` and
`Poller.wait(int, Duration)`.

```java
public record PollEvent(@Nullable Socket socket,
                        @Nullable Integer fd,
                        @Nullable Timer timer,
                        Object tag,
                        EnumSet<PollEventFlag> events,
                        EnumSet<PollEventFlag> revents) {}
```

For socket registrations, `socket()` is present and `fd()` / `timer()` are
`null`. For file descriptor registrations, `fd()` carries the registered
descriptor and `socket()` / `timer()` are `null`. For timer registrations,
`timer()` carries the registered timer. `tag()` is the optional user object
supplied at registration time.
`revents()` is the ready event set reported by the core; `events()` is the
originally requested event set when the binding has it.

---

## Timer

### Timer

Interval timer with optional spot integration.
Implements `AutoCloseable`.

```java
public final class Timer implements AutoCloseable {
    Timer();

    static Timer fromSpot(Spot spot);                                // @throws ConfigException

    void start(Duration interval, long repeatCount);                 // @throws ConfigException
    void stop();                                                     // @throws ConfigException
    long recv();                                                     // @throws RecvException
    void onFire(TimerHandler handler);                               // @throws HandlerException

    void close();                                                    // @throws CloseException
}
```

`start(...)` accepts a Java `Duration`; the binding converts it to the
nanosecond interval used by core without truncation. `recv()` returns the
timer fire count reported by core. No `RecvFlags` overload is exposed because
the core timer receive API has no flags parameter. If no timer fire is pending,
`recv()` raises `RecvException` with `RecvResult.NO_DATA`.

### TimerHandler

```java
@FunctionalInterface
public interface TimerHandler {
    void onFire(Timer timer, long fireCount);
}
```

---

## Utilities

### Zlink

Static utility class for global library operations.

```java
public final class Zlink {
    private Zlink() {}

    // Raw errno is not public. Access OS-level errno through
    // ZlinkException.getInternalErrno() on the caught exception.

    /// Return a human-readable string for the given error number.
    public static String strerror(int errnum);

    /// Return the runtime library version as [major, minor, patch].
    public static int[] version();

    /// Check if the library supports a given capability (e.g. "ipc", "tls").
    public static boolean has(String capability);

    /// Start a built-in proxy between frontend and backend sockets.
    /// An optional capture socket receives copies of all messages.
    public static void proxy(Socket frontend, Socket backend, Socket capture);  // @throws ConfigException

    /// Start a steerable proxy with an additional control socket.
    public static void proxySteerable(Socket frontend, Socket backend,
                                      Socket capture, Socket control);          // @throws ConfigException

    /// Sleep for the given duration.
    public static void sleep(Duration duration);

}
```

### Stopwatch

High-resolution stopwatch. Implements `AutoCloseable`.

```java
public final class Stopwatch implements AutoCloseable {
    Stopwatch();

    /// Return elapsed time without stopping.
    Duration intermediate();

    /// Stop the stopwatch and return total elapsed time.
    Duration stop();

    void close();                                                    // @throws CloseException
}
```

### ZlinkThread

Background thread managed by the C library. Implements `AutoCloseable`.

```java
public final class ZlinkThread implements AutoCloseable {
    ZlinkThread(Runnable task);

    /// Wait for the thread to finish and release its handle.
    void join();                                                     // @throws ConfigException

    void close();                                                    // @throws CloseException
}
```

### AtomicCounter

Lock-free atomic counter. Implements `AutoCloseable`.

```java
public final class AtomicCounter implements AutoCloseable {
    AtomicCounter();

    void set(int value);
    int increment();
    int decrement();
    int value();

    void close();                                                    // @throws CloseException
}
```

## Core API Surface 6.0.0 Alignment

Actor create and join payloads use aggregate multipart payloads. Public binding APIs accept a message collection for remote actor create, actor join, actor join receive, and actor join reply. A single-message convenience path may remain, but it must call the multipart path internally so empty payload and one empty message stay distinguishable. Admission handlers receive a borrowed payload view that is valid only during the callback.

Registry scalar configuration uses the registry option surface as the canonical API. Bindings expose typed options for registry id, heartbeat interval, heartbeat timeout, and broadcast interval. Existing named setters may remain as compatibility aliases and must delegate to the option API.
