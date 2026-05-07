[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Node / TypeScript Binding Specification

This document defines the complete public API surface of the zlink Node/TypeScript
binding. Every class, method, and type listed here is part of the contract that
the binding must expose. Internal/private members are omitted.

Only symbols reachable through the package public entrypoint are part of the
contract. Deep imports into source files, native bridge helpers, and other
non-exported modules are internal. Package `exports` should expose only the
documented public surface. Perf, samples, and tests must import the public
package entrypoint only.

## Design Basis

The Node/TypeScript binding follows the repository POSD design policy. Public
classes must hide native sequencing, ownership, and option encoding behind
typed, deep interfaces so callers do not need core implementation details.

The public package surface must model stable domain concepts, not native addon
steps. Public classes and exported types are justified when they own
context/socket lifetime, message ownership, receive metadata, service
membership, callbacks, or typed options. Native object handles, part-loop
sequencing, request tokens, callback userdata, and raw option encoding stay
inside non-exported modules.

Design review uses these POSD constraints:

- shared send/recv, nonblocking, ownership, and error mapping rules are
  centralized instead of copied across socket classes
- canonical result and facade methods do not ask callers to pass state already
  captured by the object, such as a source socket, request sequence, or
  service address
- compatibility exports, if retained, are not the canonical API and are not
  used by new docs, samples, or tests
- an exported wrapper that only forwards to the native addon without adding
  validation, ownership, lifetime, or result-shape semantics is too shallow and
  must be removed or kept private

---

## High-Performance Requirements

The Node/TypeScript binding is part of a high-performance messaging library.
Hot paths must not use reflection-style property walking, dynamic dispatch by
string lookup, unnecessary allocation, avoidable `Buffer` copies, coarse lock
or worker-thread contention, hidden waits, sleeps, busy waits, or thread joins.
Native addon code must construct public `Message` and result objects directly
from the core `*_part` substrate and must not create native aggregate arrays
only to copy them into JavaScript arrays.

## Core Alignment Rules

The detailed sections below are the canonical Node/TypeScript binding
contract. This section states cross-cutting constraints once so the per-type
API lists can stay focused on signatures.

- `PairSocket`, `DealerSocket`, and `RouterSocket` keep their documented
  send, recv, request, and reply methods, but they do not expose direct
  data-plane receive callbacks such as `onReceive(...)`.
- `SubSocket` and `XSubSocket` are receive-only topic sockets and do not
  expose direct topic callbacks such as `onSubscribe(...)`.
- `StreamSocket` keeps `recv(...)` and exposes a packet callback surface
  mapped to `zlink_stream_packet_handler()` as `onPacket(...)`.
- `SpotNode` must expose channel-aware attachment APIs:
  `attachDiscovery(discovery)`,
  `attachChannelDealer(discovery, dealer)`,
  `attachChannelDealerManual(channelName, dealer)`, and
  `attachPubIngress(pub)`.
- `Spot` must expose channel-aware data-plane operation builders:
  `sendChannel(...)`, `sendToSpot(...)`, `requestChannel(...)`, and
  `publish(serviceName, topic)`.
- `Spot.subscribe(...)` returns a service-aware `TopicMessage`.
  `TopicMessage` therefore needs `serviceName: string | null`, populated for
  SPOT subscribe results and `null` for raw `SUB` / `XSUB`.
- `Spot` must not expose `onSubscribe(...)`.
- `SUBSCRIBE_READABLE` and `ROUTED_READABLE` are readiness notifications, not
  one-event-per-message delivery counters. Binding docs and samples must drain
  until the recv path reports no data / `EAGAIN`.
- Peer weight is exposed only on `RouterSocket` and `DealerSocket` through
  typed option/property surfaces. The value range is `0..100`, default
  `100`; `0` drains new outbound selection. Submit
  attempts to a weight-`0` peer throw `SubmitError` whose `code` equals
  `SubmitResult.NotAdmitted`.
- `POLLOUT` is a send-recovery readiness signal, shared with
  `onSendReady(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `mandatory =
  true`, `handover = false`, `nodrop = true`.
- SPOT admission HWM defaults follow the core header. Router and pubsub
  admission profile/numeric options are exposed through `SpotNode`; relay and
  delivery HWM stay `0` and are not public Node/TypeScript options.
- SPOT dispatch worker min/max are `SpotNode` callback worker-pool options.
  They are not context options and must not be described as transport I/O
  threads.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routingId, advertiseEndpoint)`. Users do not configure this.

## Actor Dispatch Public Surface

Node exposes Actor dispatch through the package public entrypoint only.

```typescript
const ActorCreateStatus = Object.freeze({
    Created: 1,
    Existing: 2
});
type ActorCreateStatus =
    typeof ActorCreateStatus[keyof typeof ActorCreateStatus];

const ActorAdmissionResult = Object.freeze({
    Accept: 1,
    Reject: 2
});
type ActorAdmissionResult =
    typeof ActorAdmissionResult[keyof typeof ActorAdmissionResult];

type ActorRef = {
    nodeRid: RoutingId;
    actorId: string;
    generation: bigint;
};
type ActorCreateResult = { status: ActorCreateStatus; actor: ActorRef };
type ActorRoute = { actor: ActorRef; joined: boolean; joinedSpotRid: RoutingId | null };
type ActorRecvInfo = { actor: ActorRef; sourceNodeRid: RoutingId; sourceSessionRid: RoutingId; flags: number };
type ActorJoinInfo = {
    sourceActor: ActorRef;
    targetActor: ActorRef;
    sourceNodeRid: RoutingId;
    sourceSpotRid: RoutingId;
    targetNodeRid: RoutingId;
    targetSpotRid: RoutingId;
    joinEpoch: bigint;
    flags: number;
};
type ActorPart = { info: ActorRecvInfo; message: Message; more: boolean };
type ActorJoinRequest = { info: ActorJoinInfo; message: Message };
```

`ActorJoinRequest` carries the public join information and message. The native
reply context needed by `replyActorJoin(...)` is retained inside the binding and
is not exposed as a public field.

`SpotNode` exposes `createActor`, `actorLookup`, `remoteActorRef`,
`createRemoteActor`, `destroyActor`, `onActorAdmission`, `joinActor`,
`leaveActor`, `spotsSnapshot`, and `actorsSnapshot`. `Spot` exposes
`recvActorJoin`, `replyActorJoin`, and `actorsSnapshot`. `StreamSocket`
exposes `bindActor`, `unbindActor`, and `sendBoundActor`. `Actor` exposes
`actorRef`, `ref`, `join`, `leave`, `recvPart`, `sendBoundSession`,
`closeBoundSession`, and `close`. `Discovery` exposes `resolveActor`.

Actor ids are non-empty UTF-8 strings up to 255 bytes and must not contain NUL.
`generation === 0n` is an unchecked remote ref. Actor readable dispatch returns
preloaded parts through the dispatch info using the callback lifetime ActorRef
subject. Actor creation places the Actor in the Entry Spot. Joining a user Spot
requires a bound STREAM session. One Actor binds to only one STREAM session, and
one STREAM session can bind multiple Actors. `leave` does not drain unread Actor
messages. Remote Actor create-or-get calls the admission handler only when the
target Actor does not already exist.

## Core

### Context

```typescript
class Context {
    constructor();
    readonly options: ContextOptions;
    /** @throws {CloseError} */
    shutdown(): void;
    /** @throws {ConfigError} */
    recalculateAutoHwm(): void;
    /** @throws {CloseError} */
    close(): void;
}
```

### ContextOptions

All accessor pairs below throw `ConfigError` on failure.

```typescript
class ContextOptions {
    ioThreads: number;          // get / set — @throws {ConfigError}
    maxSockets: number;         // get / set — @throws {ConfigError}
    readonly socketLimit: number;           // @throws {ConfigError}
    maxMsgSize: number;         // get / set — @throws {ConfigError}
    readonly msgTSize: number;              // @throws {ConfigError}
    threadPriority: number;     // get / set — @throws {ConfigError}
    threadSchedulingPolicy: number; // get / set — @throws {ConfigError}
    threadNamePrefix: string;   // get / set — @throws {ConfigError}
    blocky: boolean;            // get / set — @throws {ConfigError}
    autoHwmEnabled: boolean;    // get / set — @throws {ConfigError}
    autoHwmRecalcDebounceMs: number; // get / set — @throws {ConfigError}
    autoHwmProfile: AutoHwmProfileValue; // get / set — @throws {ConfigError}
    /** @throws {ConfigError} */
    addThreadAffinity(cpu: number): void;
    /** @throws {ConfigError} */
    removeThreadAffinity(cpu: number): void;
}
```

```typescript
const AutoHwmProfile = Object.freeze({
    Compact: 0,
    LowLatency: 1,
    Balanced: 2,
    Throughput: 3
});
type AutoHwmProfileValue =
    typeof AutoHwmProfile[keyof typeof AutoHwmProfile];
```

### Module-Level

```typescript
function version(): [number, number, number];

// Module-level errno() is NOT public. Access internal errno via
// ZlinkError.internalErrno on the caught exception.

/// Return a human-readable string for the given error number.
function strerror(code: number): string;

/// Check if the library supports a given capability (e.g. "ipc", "tls").
function has(capability: string): boolean;

/**
 * Start a built-in proxy between frontend and backend sockets.
 * @throws {ConfigError}
 */
function proxy(frontend: BaseSocket, backend: BaseSocket,
               capture?: BaseSocket): void;

/**
 * Start a steerable proxy with an additional control socket.
 * @throws {ConfigError}
 */
function proxySteerable(frontend: BaseSocket, backend: BaseSocket,
                        capture: BaseSocket | null,
                        control: BaseSocket): void;

/// Sleep for the given number of seconds.
function sleep(seconds: number): void;

/**
 * Close all parts in a multipart message array.
 * @throws {ConfigError}
 */
function multipartClose(parts: Message[]): void;
```

---

## Socket Types

All socket classes expose `bind()`, `unbind()`, `monitorOpen()`, and `close()`.
They also expose common TLS helpers `setTlsServer(...)` and
`setTlsClient(...)`.
Connectable sockets also expose `connect()`, `disconnect()`, and
`disconnectRid()`. `StreamSocket` is bind-only and does not expose those
connectable-socket methods.

```typescript
type BaseSocket =
    PairSocket | PubSocket | SubSocket | DealerSocket | RouterSocket |
    XPubSocket | XSubSocket | StreamSocket;
```

Node / TypeScript nonblocking data-plane helpers follow this rule:

- `send(...)` and `publish(...)` return `false` only for temporary
  backpressure when `SendFlags.DontWait` is used.
- Blocking submit returns `true` on success. Route-not-ready and other submit
  failures still throw `SubmitError`.
- `recv(...)`, `subscribe(...)`, `receiveSubscriptionEvent(...)`,
  `recvRouted(...)`, monitor `recv(...)`, and timer `recv()` return `null`
  when the core reports no data and still throw `RecvError` for real recv
  failures.

Peer weight is not a common socket option. Bindings expose `peerWeight` only on
`RouterSocket` and `DealerSocket`:

```typescript
// No common peer-weight accessor. RouterSocket and DealerSocket expose peerWeight on their typed option facade.
```

After `attachDiscovery(...)` succeeds on a socket, `connect(...)`,
`disconnect(...)`, `disconnectRid(...)`, `unbind(...)`, and `close()` on that
socket fail with `ConfigError` / `ConnectError` / `CloseError` according to the
called function family. The attached `Discovery` owns the participant lifecycle.

### PairSocket

```typescript
class PairSocket {
    constructor(ctx: Context);
    readonly options: CommonSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectRid(routingId: RoutingId): void;
    /** @throws {SubmitError} */
    send(message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    send(parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received | null;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket;
    /** @throws {CloseError} */
    close(): void;
}
```

### PubSocket

```typescript
class PubSocket {
    constructor(ctx: Context);
    readonly options: PubSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectRid(routingId: RoutingId): void;
    /** @throws {SubmitError} */
    publish(topic: string, message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;
    /** @throws {ConfigError} */
    monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket;
    /** @throws {CloseError} */
    close(): void;
}
```

### SubSocket

```typescript
class SubSocket {
    constructor(ctx: Context);
    readonly options: SubSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectRid(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    setSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    unsetSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    subscriptionAt(index: number): SubscriptionEntry | null;
    /** @throws {RecvError} */
    subscribe(flags?: RecvFlags): TopicMessage | null;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;
    /** @throws {ConfigError} */
    monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket;
    /** @throws {CloseError} */
    close(): void;
}
```

### DealerSocket

```typescript
class DealerSocket {
    constructor(ctx: Context);
    readonly options: DealerSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectRid(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    setRoutingId(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    getRoutingId(): RoutingId;
    /** @throws {ConfigError} */
    setChannelName(channelName: string): void;
    /** @throws {ConfigError} */
    getChannelName(): string;
    /** @throws {SubmitError} */
    send(message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    send(parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received | null;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;
    /** @throws {ConfigError} */
    monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket;

    // --- dealer request (async) — no flags, timeout = 0 uses socket default ---
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    request(message: MessageLike, timeout?: number): Promise<Message[]>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    request(parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;

    // --- dealer request (callback submit) — timeout = 0 uses socket default ---
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(message: MessageLike,
            callback: RequestCallback,
            flags?: SendFlags,
            timeout?: number): boolean;
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(parts: readonly MessageLike[],
            callback: RequestCallback,
            flags?: SendFlags,
            timeout?: number): boolean;

    /** @throws {CloseError} */
    close(): void;
}
```

### RouterSocket

```typescript
class RouterSocket {
    constructor(ctx: Context);
    readonly options: RouterSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectRid(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    setRoutingId(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    getRoutingId(): RoutingId;
    /** @throws {SubmitError} */
    send(routingId: RoutingId, message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    send(routingId: RoutingId, parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received | null;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;

    // --- router request (async) — no flags, timeout = 0 uses socket default ---
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    request(peerRid: RoutingId, message: MessageLike,
            timeout?: number): Promise<Message[]>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    request(peerRid: RoutingId, parts: readonly MessageLike[],
            timeout?: number): Promise<Message[]>;

    // --- router request (callback submit) — timeout = 0 uses socket default ---
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(peerRid: RoutingId, message: MessageLike,
            callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(peerRid: RoutingId, parts: readonly MessageLike[],
            callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;

    // --- router reply ---
    /** @throws {SubmitError} */
    reply(peerRid: RoutingId, requestSeq: bigint, message: MessageLike,
          flags?: SendFlags): void;
    /** @throws {SubmitError} */
    reply(peerRid: RoutingId, requestSeq: bigint, parts: readonly MessageLike[],
          flags?: SendFlags): void;

    // --- router → spot routed send ---
    /** @throws {SubmitError} */
    sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
               message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
               parts: readonly MessageLike[], flags?: SendFlags): boolean;

    // --- router → spot routed request (async) — no flags ---
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                  message: MessageLike, timeout?: number): Promise<Message[]>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                  parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;

    // --- router → spot routed request (callback submit) ---
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                  message: MessageLike,
                  callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                  parts: readonly MessageLike[],
                  callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;

    // --- router → spot routed reply ---
    /** @throws {SubmitError} */
    replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;

    // NOTE: RouterSocket has one routed receive surface. recv receives both
    // regular ROUTER traffic and spot-origin routed traffic.
    // `Received.routingId` is source_node_rid, and `Received.spotRid` is set
    // only for spot-origin traffic. ROUTER does not expose a data-plane
    // callback install surface such as onReceive. Request completion remains
    // available only through request().

    /** @throws {ConfigError} */
    monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket;
    /** @throws {CloseError} */
    close(): void;
}
```

### XPubSocket

```typescript
class XPubSocket {
    constructor(ctx: Context);
    readonly options: PubSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectRid(routingId: RoutingId): void;
    /** @throws {SubmitError} */
    publish(topic: string, message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {RecvError} */
    receiveSubscriptionEvent(flags?: RecvFlags): SubscriptionEvent | null;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket;
    /** @throws {CloseError} */
    close(): void;
}
```

### XSubSocket

```typescript
class XSubSocket {
    constructor(ctx: Context);
    readonly options: SubSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectRid(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    setSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    unsetSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    subscriptionAt(index: number): SubscriptionEntry | null;
    /** @throws {RecvError} */
    subscribe(flags?: RecvFlags): TopicMessage | null;
    /** @throws {ConfigError} */
    monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket;
    /** @throws {CloseError} */
    close(): void;
}
```

### StreamSocket

```typescript
class StreamSocket {
    constructor(ctx: Context);
    readonly options: StreamSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConfigError} */
    setRoutingId(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    getRoutingId(): RoutingId;
    /** @throws {SubmitError} */
    send(routingId: RoutingId, message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    send(routingId: RoutingId, parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /**
     * Two public receive modes on the same StreamSocket:
     *   (1) recv(), (2) onPacket(handler). Second attach throws
     *   HandlerError(HandlerResult.Busy).
     * @throws {RecvError}
     */
    recv(flags?: RecvFlags): Received | null;
    /**
     * Mode (2): framed packet callback mapped to
     * `zlink_stream_packet_handler()`. Wire frame is big-endian u16
     * header_size + u32 body_size + header + body. Handler receives the
     * source routing id, a header Message, and a body Message; both
     * messages transfer ownership to the handler.
     * @throws {HandlerError}
     */
    onPacket(handler: StreamPacketHandler): void;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {RequestError} */
    bindActor(node: SpotNode, sessionRid: RoutingId,
              actor: ActorRef, timeoutMs?: number): void;
    /** @throws {RequestError} */
    unbindActor(node: SpotNode, sessionRid: RoutingId,
                actorId: string, timeoutMs?: number): void;
    /** @throws {SubmitError} */
    sendBoundActor(node: SpotNode, sessionRid: RoutingId,
                   actorId: string, message: MessageLike,
                   flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    sendBoundActor(node: SpotNode, sessionRid: RoutingId,
                   actorId: string, parts: readonly MessageLike[],
                   flags?: SendFlags): boolean;
    /** @throws {ConfigError} */
    monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket;
    /** @throws {CloseError} */
    close(): void;
}
```

### Socket Option Classes

All accessor pairs (both getters and setters) in the classes below throw
`ConfigError` on failure.

```typescript
class CommonSocketOptions {
    linger: number;              // get / set (ms)
    sendHwm: number;             // get / set
    recvHwm: number;             // get / set
    sendTimeout: number;         // get / set (ms)
    recvTimeout: number;         // get / set (ms)
    immediate: boolean;          // get / set
    connectTimeout: number;      // get / set (ms)
    ipv6: boolean;               // get / set
    tcpNoDelay: boolean;         // get / set
    tcpKeepalive: number;        // get / set
    heartbeatInterval: number;   // get / set (ms)
    heartbeatTtl: number;        // get / set (ms)
    heartbeatTimeout: number;    // get / set (ms)
    maxMsgSize: bigint;          // get / set
    backlog: number;             // get / set
    reconnectInterval: number;   // get / set (ms)
    reconnectIntervalMax: number;// get / set (ms)
    ridDuplicatePolicy: RidDuplicatePolicyValue; // get / set
    autoHwmMsgUnitBytes: number;  // get / set
    readonly lastEndpoint: string;
}

class DealerSocketOptions extends CommonSocketOptions {
    probe: boolean;              // set only
    requestTimeout: number;      // set only (ms)
    peerWeight: number;          // set only, 0..100
}

class RouterSocketOptions extends CommonSocketOptions {
    mandatory: boolean;          // get / set
    handover: boolean;           // get / set
    probe: boolean;              // get / set
    readonly connectRoutingId: RoutingId | null;
    setConnectRoutingId(routingId: RoutingId): void;
    requestTimeout: number;      // get / set (ms)
    peerWeight: number;          // get / set, 0..100
}

class PubSocketOptions extends CommonSocketOptions {
    verbose: boolean;            // get / set
    verboser: boolean;           // get / set
    noDrop: boolean;             // get / set
    manual: boolean;             // get / set
    manualLastValue: boolean;    // get / set
    readonly topicsCount: number;
    welcomeMessage(): Message;
    setWelcomeMessage(message: MessageLike): void;
    approveSubscribe(routingId: RoutingId): void;
    rejectSubscribe(routingId: RoutingId): void;
}

class SubSocketOptions extends CommonSocketOptions {
    readonly topicsCount: number;
}

class StreamSocketOptions extends CommonSocketOptions {
    notify: boolean;             // get / set
}

```

---

## Message / Domain

### Message

```typescript
class Message {
    /** @throws {ConfigError} */
    constructor(data: BufferLike);
    // Public input adapters are copy-based only; borrowed external-wrap APIs
    // are intentionally not exposed on managed bindings.
    /** @throws {ConfigError} */
    static from(data: BufferLike): Message;
    /** @throws {ConfigError} */
    data(): Buffer;
    /** @throws {ConfigError} */
    size(): number;
    /** @throws {ConfigError} */
    getProperty(name: string): string | null;
    /** @throws {ConfigError} */
    refCount(): number;
    /** @throws {ConfigError} */
    close(): void;
}

type MessageLike = Message | Buffer | Uint8Array | string;
type BufferLike = Buffer | Uint8Array | string;
```

### Codec Extensions

Codec adapters are separate public extension packages layered on top of the
core package. Their contract lives in
[Node Codec Extension Specification](codec.md). The root `@ulalax/zlink`
entrypoint does not expose codec entrypoints or require codec dependencies.

### RoutingId

Routing-id value object. Binary-safe, 1-255 bytes, immutable. The binding
exposes this wrapper type (not a raw `Buffer`) everywhere a routing id
appears in the public surface.

```typescript
class RoutingId {
    /** @throws {ConfigError} */
    static fromBytes(bytes: Buffer | Uint8Array): RoutingId;
    /** Parses the hex string returned by `toHex()` / `toString()`.
     * The hex input must be at most 510 chars and decode to 1-255 bytes.
     * @throws {ConfigError}
     */
    static fromString(value: string): RoutingId;
    toBytes(): Buffer;
    readonly size: number;             // byte length (1-255)
    equals(other: RoutingId): boolean;
    /** Hex-encoded representation (lower-case, no separators). */
    toHex(): string;
    /** Alias of `toHex()` for convenient string conversion. */
    toString(): string;
}
```

Rules:
- `RoutingId` is binary-safe; there is no `RoutingId(string)` constructor.
  Use `fromBytes` for raw bytes. `fromString(value)` only parses the
  even-length hex display form returned by `toHex()` / `toString()`.
  Hex strings longer than 510 chars, or values that decode above 255 bytes,
  fail with `ConfigError`.
- `RoutingId` instances are immutable; `toBytes()` returns a copy or an
  immutable view.
- Any socket or service method that takes a routing id accepts a
  `RoutingId` value (not a raw `Buffer`).

### Received

Recv result for PAIR / DEALER / ROUTER / STREAM and router/spot routed
recv paths. Canonical shape shared with `TopicMessage` except that
`Received` has no `topic` field.

```typescript
class Received {
    readonly routingId: RoutingId | null;    // peer_rid (Router) / source_node_rid (Spot)
    readonly spotRid: RoutingId | null;      // set only for SPOT routed recv
    readonly requestSeq: bigint | null;      // set on request-reply recv paths, else null
    readonly parts: Message[];

    isSinglePart(): boolean;
    /** @throws {RecvError} */
    firstPart(): Message;
    /** @throws {RecvError} */
    singlePartOrThrow(): Message;

    /** reply requires requestSeq; null or invalid reply context raises SubmitError. */
    /** @throws {SubmitError} */
    reply(part: Message): void;
    /** @throws {SubmitError} */
    reply(part: Message, flags: SendFlags): void;
    /** @throws {SubmitError} */
    reply(parts: Message[]): void;
    /** @throws {SubmitError} */
    reply(parts: Message[], flags: SendFlags): void;

    /** @throws {CloseError} */
    close(): void;
}
```

### TopicMessage

Topic-aware recv result used by SUB / XSUB / Spot subscribe paths.

```typescript
class TopicMessage {
    readonly routingId: RoutingId | null;    // null when transport carries no source id
    readonly serviceName: string | null;     // Spot subscribe only; null for raw SUB / XSUB
    readonly topic: string;                  // UTF-8
    readonly parts: Message[];
    isSinglePart(): boolean;
    /** @throws {RecvError} */
    firstPart(): Message;
    /** @throws {RecvError} */
    singlePartOrThrow(): Message;
    /** @throws {CloseError} */
    close(): void;
}
```

### SubscriptionEvent

Value object emitted by `XPubSocket.receiveSubscriptionEvent` and
`Spot.receiveSubscriptionEvent`. No lifecycle methods.

```typescript
class SubscriptionEvent {
    readonly routingId: RoutingId | null;
    readonly serviceName: string | null;
    readonly topic: string;                  // UTF-8
    readonly subscribed: boolean;            // true=subscribe, false=unsubscribe
}
```

### SubscriptionEntry

Value object returned by subscription introspection methods.

```typescript
type SubscriptionEntry = {
    readonly filter: string;
    readonly isPattern: boolean;
};
```

### SendFlags

```typescript
const SendFlags = {
    None: 0,
    DontWait: 1,
} as const;

type SendFlags = typeof SendFlags[keyof typeof SendFlags];
```

### RecvFlags

```typescript
const RecvFlags = {
    None: 0,
    DontWait: 1,
} as const;

type RecvFlags = typeof RecvFlags[keyof typeof RecvFlags];
```

### RidDuplicatePolicy

```typescript
const RidDuplicatePolicy = {
    Reject: 0,
    Handover: 1,
} as const;

type RidDuplicatePolicyValue =
    typeof RidDuplicatePolicy[keyof typeof RidDuplicatePolicy];
```

### SubmitResult

Result codes for send/request/reply/publish operations.
All failures throw `SubmitError` (a `ZlinkError` subclass) whose
`.result` is the specific `SubmitResult` value and whose `.code`
is the corresponding globally unique error code.

```typescript
const SubmitResult = {
    Ok: 0,
    Backpressured: 1,
    NotConnected: 2,
    NotFound: 3,
    Terminated: 4,
    InvalidHandle: 5,
    InvalidArgument: 6,
    NotSupported: 7,
    InvalidState: 8,
    ThreadViolation: 9,
    OutOfMemory: 10,
    SeqExhausted: 11,
    InternalError: 12,
    NotAdmitted: 13,     // target peer has weight 0
} as const;

type SubmitResult = typeof SubmitResult[keyof typeof SubmitResult];
```

### RequestResult

Result codes for request completion callbacks.

```typescript
const RequestResult = {
    Ok: 0,
    TimedOut: 101,
    NotFound: 102,
    Terminated: 103,
    ProtocolError: 104,
    InternalError: 105,
    Rejected: 106,
    Conflict: 107,
    Busy: 108,
    NotConnected: 109,
    InvalidArgument: 110,
    InvalidState: 111,
    NotSupported: 112,
} as const;

type RequestResult = typeof RequestResult[keyof typeof RequestResult];
```

### RecvResult

Result codes for recv, subscribe, and subscription event operations.

```typescript
const RecvResult = {
    Ok: 0,
    NoData: 201,
    Busy: 202,
    Terminated: 203,
    InvalidHandle: 204,
    NotSupported: 205,
    InternalError: 206,
} as const;

type RecvResult = typeof RecvResult[keyof typeof RecvResult];
```

### HandlerResult

Result codes for handler registration operations (`onPacket`,
`onSendReady`, `onRoutedReceive`, `onDispatchEvent`, `onEvent`, etc.).

```typescript
const HandlerResult = {
    Ok: 0,
    InvalidArgument: 301,
    Busy: 302,
    NotSupported: 303,
    Deadlock: 304,
    InvalidHandle: 305,
    InternalError: 306,
} as const;

type HandlerResult = typeof HandlerResult[keyof typeof HandlerResult];
```

### CloseResult

Result codes for close and destroy operations.

```typescript
const CloseResult = {
    Ok: 0,
    Busy: 401,
    Shutdown: 402,
    InvalidHandle: 403,
    InternalError: 404,
} as const;

type CloseResult = typeof CloseResult[keyof typeof CloseResult];
```

### BindResult

Result codes for bind operations.

```typescript
const BindResult = {
    Ok: 0,
    InvalidArgument: 501,
    AddrInUse: 502,
    NotSupported: 503,
    InvalidHandle: 504,
    InternalError: 505,
} as const;

type BindResult = typeof BindResult[keyof typeof BindResult];
```

### ConnectResult

Result codes for connect, disconnect, and unbind operations.

```typescript
const ConnectResult = {
    Ok: 0,
    InvalidArgument: 601,
    NotSupported: 602,
    InvalidHandle: 603,
    InternalError: 604,
    NotFound: 605,
    Conflict: 606,
    Busy: 607,
} as const;

type ConnectResult = typeof ConnectResult[keyof typeof ConnectResult];
```

### ConfigResult

Result codes for configuration, option, and snapshot operations.

```typescript
const ConfigResult = {
    Ok: 0,
    InvalidHandle: 701,
    InvalidArgument: 702,
    NotSupported: 703,
    InternalError: 704,
    InvalidState: 705,
    NotFound: 706,
} as const;

type ConfigResult = typeof ConfigResult[keyof typeof ConfigResult];
```

### ZlinkError

Common base class of all zlink errors. Every failing operation throws one
of the eight concrete subclasses below, each of which corresponds to a C
API function-category result enum (`SubmitError`, `RequestError`,
`RecvError`, `HandlerError`, `CloseError`, `BindError`, `ConnectError`,
`ConfigError`). Catch `ZlinkError` for the "catch-all" idiom, or a
specific subclass when finer-grained handling is required. Method
declarations indicate which specific subclass they throw via TSDoc
`@throws` comments.

The `code` field is a globally unique `int` that spans all result enum
ranges (0-706). The code alone identifies the error without needing to
know which enum it belongs to. `internalErrno` carries the OS-level
errno when available (0 otherwise).

```typescript
class ZlinkError extends Error {
    readonly code: number;
    readonly internalErrno: number;
    constructor(code: number, internalErrno?: number);
}
```

### SubmitError

Thrown by `send` / `publish` / `reply` / `request` (callback submit)
operations. Wraps a `SubmitResult`.

```typescript
class SubmitError extends ZlinkError {
    constructor(result: SubmitResult, internalErrno?: number);
    readonly result: SubmitResult;
}
```

### RequestError

Thrown by request completion paths (Promise/async variants) and used as
the category for request-specific failures. Wraps a `RequestResult`.
Callback-style `request(...)` methods deliver `RequestResult` directly
to the callback (`RequestCallback`) rather than throwing this
error.

```typescript
class RequestError extends ZlinkError {
    constructor(result: RequestResult, internalErrno?: number);
    readonly result: RequestResult;
}
```

### RecvError

Thrown by `recv` / `subscribe` / `receiveSubscriptionEvent` / monitor
`recv` / timer `recv` operations. Wraps a `RecvResult`.

```typescript
class RecvError extends ZlinkError {
    constructor(result: RecvResult, internalErrno?: number);
    readonly result: RecvResult;
}
```

### HandlerError

Thrown by handler registration methods (`onPacket`, `onSendReady`,
`onRoutedReceive`, `onDispatchEvent`, `onFire`, `onEvent`, etc.). Wraps a
`HandlerResult`.

```typescript
class HandlerError extends ZlinkError {
    constructor(result: HandlerResult, internalErrno?: number);
    readonly result: HandlerResult;
}
```

### CloseError

Thrown by `close()` / `destroy()` / `shutdown()` operations. Wraps a
`CloseResult`.

```typescript
class CloseError extends ZlinkError {
    constructor(result: CloseResult, internalErrno?: number);
    readonly result: CloseResult;
}
```

### BindError

Thrown by `bind(...)` operations. Wraps a `BindResult`.

```typescript
class BindError extends ZlinkError {
    constructor(result: BindResult, internalErrno?: number);
    readonly result: BindResult;
}
```

### ConnectError

Thrown by `connect(...)`, `disconnect(...)`, `unbind(...)`, and
`connectPeer(...)` / `disconnectPeer(...)` / `connectRegistry(...)`
operations. Wraps a `ConnectResult`.

```typescript
class ConnectError extends ZlinkError {
    constructor(result: ConnectResult, internalErrno?: number);
    readonly result: ConnectResult;
}
```

### ConfigError

Thrown by option get/set, snapshot, poller mutation, proxy, timer
configuration (`start` / `stop`), TLS setup (`setTls*`), discovery
attach, message lifecycle, and routing-id accessor operations. Wraps a
`ConfigResult`.

```typescript
class ConfigError extends ZlinkError {
    constructor(result: ConfigResult, internalErrno?: number);
    readonly result: ConfigResult;
}
```

---

## Monitoring

### MonitorSocket

Starts in recv model. `onEvent(...)` transitions one-way to callback-only
model; after that `recv()` throws a busy recv error and `snapshot()` still works.

```typescript
class MonitorSocket {
    /**
     * No-op callback for callback-only model. Pass to onEvent() to keep a
     * valid handler when the application does not care about events; once
     * installed the monitor is in callback-only model and recv() throws a
     * busy recv error (snapshot() still works). To drive the monitor via
     * snapshot() / recv() instead, leave onEvent unset.
     * Maps to zlink_monitor_ignore_handler.
     */
    static readonly ignoreHandler: SocketMonitorHandler;

    /** @throws {RecvError} */
    recv(flags?: RecvFlags): MonitorEvent | null;
    /** @throws {HandlerError} */
    onEvent(handler: (event: MonitorEvent) => void): void;
    /** @throws {ConfigError} */
    snapshot(): MonitorSnapshot;
    /** @throws {CloseError} */
    close(): void;
}
```

### MonitorEvent

Socket monitor event. Canonical shape shared with every other binding.

```typescript
class MonitorEvent {
    readonly event: MonitorEventType;        // CONNECTION_READY, CONNECTED, DISCONNECTED, PEER_WEIGHT_CHANGED, ...
    readonly value: number;                  // uint32 — event-specific payload (PEER_WEIGHT_CHANGED carries the new 0..100 weight)
    readonly routingId: RoutingId | null;    // peer routing id (null when not applicable)
    readonly localAddr: string;              // local endpoint
    readonly remoteAddr: string;             // remote endpoint
}
```

```typescript
const MonitorEventType = {
    Connected: 1 << 0,
    ConnectDelayed: 1 << 1,
    ConnectRetried: 1 << 2,
    Listening: 1 << 3,
    BindFailed: 1 << 4,
    Accepted: 1 << 5,
    AcceptFailed: 1 << 6,
    Closed: 1 << 7,
    CloseFailed: 1 << 8,
    Disconnected: 1 << 9,
    MonitorStopped: 1 << 10,
    HandshakeFailedNoDetail: 1 << 11,
    ConnectionReady: 1 << 12,
    HandshakeFailedProtocol: 1 << 13,
    HandshakeFailedAuth: 1 << 14,
    PeerWeightChanged: 1 << 15,
} as const;

type MonitorEventType =
    typeof MonitorEventType[keyof typeof MonitorEventType];

const MonitorSourceKind = {
    Socket: 1,
    SpotPub: 3,
    SpotSub: 4,
} as const;
type MonitorSourceKindValue =
    typeof MonitorSourceKind[keyof typeof MonitorSourceKind];
```

### MonitorSnapshot

Runtime snapshot returned by `MonitorSocket.snapshot()`.

```typescript
interface MonitorSnapshot {
    readonly sourceKind: MonitorSourceKindValue;
    readonly stateFlags: number;             // uint32 bitmask
    readonly detailFlags: number;            // uint32 bitmask
    readonly sndPendingMsgs: bigint;         // uint64 send-queue depth
    readonly rcvPendingMsgs: bigint;         // uint64 recv-queue depth
    readonly autoHwmEnabled: boolean;
    readonly autoHwmProfile: number;
    readonly autoHwmRole: number;
    readonly autoHwmPolicyClass: number;
    readonly autoHwmUnitBudgetBytes: bigint;
    readonly autoHwmSizeCap: number;
    readonly autoHwmSocketMessageSlots: bigint;
    readonly autoHwmEffectiveMessageBytes: bigint;
    readonly autoHwmAppliedSndHwm: number;
    readonly autoHwmAppliedRcvHwm: number;
    readonly autoHwmEffectiveSndBuf: number;
    readonly autoHwmEffectiveRcvBuf: number;
    readonly autoHwmLastRecalcMs: bigint;
    readonly autoHwmLastRecalcReason: number;
    readonly autoHwmSendBlockedRatioPpm: number;
    readonly autoHwmDeferredSndHwm: number;
    readonly autoHwmDeferredRcvHwm: number;
    /**
     * Convenience helper for raw socket monitor sources only.
     * SPOT_PUB and SPOT_SUB sources do not extend this into SPOT readiness.
     */
    isReady(): boolean;
}
```

---

## Services

### Registry

```typescript
class Registry {
    constructor(ctx: Context);
    /** @throws {BindError} */
    bind(pubEndpoint: string, routerEndpoint: string): void;
    /** @throws {ConfigError} */
    setId(id: number): void;
    /** @throws {ConfigError} */
    addPeer(pubEndpoint: string): void;
    /** @throws {ConfigError} */
    setHeartbeat(intervalMs: number, timeoutMs: number): void;
    /** @throws {ConfigError} */
    setBroadcastInterval(intervalMs: number): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConfigError} */
    statusSnapshot(): RegistryStatus;
    /** @throws {ConfigError} */
    serviceSummarySnapshot(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[];
    /** @throws {ConfigError} */
    topologySnapshot(): RegistryTopologyEntry[];
    /** @throws {ConfigError} */
    topologyQuery(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
    /** @throws {ConfigError} */
    memberPeers(channelName: string): MemberPeerEntry[];
    /** @throws {CloseError} */
    close(): void;
}
```

### Discovery

```typescript
const AutoConnectType = {
    Invalid: 0,
    RouteMesh: 1,
    ClientServer: 2,
    DealerMesh: 3,
    Fanout: 4,
    SpotMesh: 5,
} as const;
type AutoConnectType = typeof AutoConnectType[keyof typeof AutoConnectType];

class Discovery {
    constructor(ctx: Context, autoConnectType: AutoConnectType, channelName: string);
    readonly autoConnectType: AutoConnectType;
    readonly channelName: string;
    /** @throws {ConnectError} */
    connectRegistry(endpoint: string): void;
    /** @throws {ConfigError} */
    setValue(value: number): void;
    /** @throws {ConfigError} */
    getValue(): number;
    /** @throws {ConfigError} */
    memberPeers(): MemberPeerEntry[];
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /**
     * Resolve the current owner node routing id for a logical spot routing id.
     * Maps to zlink_discovery_resolve_spot. Registry-backed lookup requires
     * the publishing Discovery to enable ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC.
     */
    resolveSpot(spotRid: RoutingId): RoutingId;
    /**
     * Resolve the current route for an actor id in this discovery channel.
     * Maps to zlink_discovery_resolve_actor.
     */
    resolveActor(actorId: string): ActorRoute;
    /** Publish SPOT owner rows to Registry when true. */
    spotOwnerSyncEnabled: boolean;
    /** Publish actor route rows to Registry when true. */
    actorRouteSyncEnabled: boolean;
    /** @throws {CloseError} */
    close(): void;
}
```

### SpotNode

```typescript
const SpotNodeMode = {
    PubSub: 1,
    Routed: 2,
    All: 3,
} as const;
type SpotNodeModeValue = typeof SpotNodeMode[keyof typeof SpotNodeMode];

class SpotNode {
    constructor(ctx: Context, mode?: SpotNodeModeValue);
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConnectError} */
    connectPeer(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectPeer(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectPeerRid(targetNodeRid: RoutingId): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;
    /** @throws {ConfigError} */
    attachChannelDealer(discovery: Discovery, dealer: DealerSocket): void;
    /** @throws {ConfigError} */
    attachChannelDealerManual(channelName: string, dealer: DealerSocket): void;
    /** @throws {ConfigError} */
    attachPubIngress(pub: PubSocket): void;
    // SpotNode admission and dispatch-worker options. These map to the six
    // public zlink_spot_node_option_t values; no raw option bag is public.
    routerHwmProfile: AutoHwmProfileValue;    // get / set — @throws {ConfigError}
    routerHwm: number;                        // get / set — @throws {ConfigError}
    pubsubHwmProfile: AutoHwmProfileValue;    // get / set — @throws {ConfigError}
    pubsubHwm: number;                        // get / set — @throws {ConfigError}
    dispatchWorkersMin: number;               // get / set — @throws {ConfigError}
    dispatchWorkersMax: number;               // get / set — @throws {ConfigError}
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
    /** @throws {ConfigError} */
    entrySpot(): Spot;
    /** @throws {ConfigError} */
    createSpot(): Spot;
    /** @throws {ConfigError} */
    spotLookup(spotRid: RoutingId): Spot | null;
    /** @throws {ConfigError} */
    createActor(actorId: string): Actor;
    /** @throws {ConfigError} */
    actorLookup(actorId: string): ActorRef;
    static remoteActorRef(targetNodeRid: RoutingId, actorId: string): ActorRef;
    /** @throws {RequestError} */
    createRemoteActor(targetNodeRid: RoutingId, actorId: string,
                      message: MessageLike, timeoutMs?: number): ActorCreateResult;
    /** @throws {RequestError} */
    destroyActor(actor: ActorRef, timeoutMs?: number): void;
    /** @throws {HandlerError} */
    onActorAdmission(handler: ActorAdmissionHandler): void;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    joinActor(actor: ActorRef, destNodeRid: RoutingId, destSpotRid: RoutingId,
              message: MessageLike, timeout?: number): Promise<Message[]>;
    /** @throws {SubmitError} */
    joinActor(actor: ActorRef, destNodeRid: RoutingId, destSpotRid: RoutingId,
              message: MessageLike, callback: RequestCallback,
              flags?: SendFlags, timeout?: number): boolean;
    /** @throws {RequestError} */
    leaveActor(actor: ActorRef, currentSpotRid: RoutingId, timeoutMs?: number): void;
    /** @throws {ConfigError} */
    statusSnapshot(): SpotNodeStatus;
    /** @throws {ConfigError} */
    peersSnapshot(): SpotNodePeerEntry[];
    /** @throws {ConfigError} */
    peersQuery(filter?: SpotNodePeerFilter): SpotNodePeerEntry[];
    /** @throws {ConfigError} */
    subjectsSnapshot(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[];
    /** @throws {ConfigError} */
    spotsSnapshot(): SpotNodeSpotEntry[];
    /** @throws {ConfigError} */
    actorsSnapshot(): SpotNodeActorEntry[];
    /** @throws {ConfigError} */
    internalSocketsSnapshot(filter?: SpotNodeSocketSnapshotFilter): SpotNodeSocketSnapshotEntry[];
    // --- identity / routing ---
    /**
     * SpotNode's logical address. Maps to zlink_set_routing_id(node, ...) /
     * zlink_get_routing_id(node, ...).
     * @throws {ConfigError}
     */
    setRoutingId(rid: RoutingId): void;
    readonly routingId: RoutingId;

    // close() cascades: closes all live Spot handles before the node becomes invalid.
    /** @throws {CloseError} */
    close(): void;
}
```

Omitting `mode` uses `SpotNodeMode.All`.

`SpotNode` owns the lifecycle. `Spot` handles are created through
`SpotNode.createSpot()`, Entry Spot facades through `SpotNode.entrySpot()`,
and lookup facades through `SpotNode.spotLookup(...)`. Direct
`new Spot(node)` construction is internal and is not part of the public API
contract.

`dispatchWorkersMin` must be at least `1`; `dispatchWorkersMax` must be at
least `dispatchWorkersMin`. If unset, core defaults are CPU count `1`:
`min=max=1`; otherwise `min=2`, `max=cpuCount`. These values size only the
SpotNode dispatch callback worker pool.

### Actor

```typescript
class Actor {
    readonly actorRef: ActorRef;
    ref(): ActorRef;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    join(spot: Spot, message: MessageLike, timeout?: number): Promise<Message[]>;
    /** @throws {SubmitError} */
    join(spot: Spot, message: MessageLike, callback: RequestCallback,
         flags?: SendFlags, timeout?: number): boolean;
    /** @throws {RequestError} */
    leave(spot: Spot, timeoutMs?: number): void;
    /** @throws {RecvError} */
    recvPart(flags?: RecvFlags): ActorPart | null;
    /** @throws {SubmitError} */
    sendBoundSession(message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {RequestError} */
    closeBoundSession(timeoutMs?: number): void;
    /** @throws {RequestError} */
    close(timeoutMs?: number): void;
}
```

`Actor` is a ref-centered public object. The public contract does not expose a
native Actor pointer. An Actor belongs to exactly one Spot at a time. Newly
created Actors start in the Entry Spot. The application cannot remove the
Entry Spot. Joining a non-entry Spot requires a STREAM session that was
successfully bound with `StreamSocket.bindActor(...)`.

### Spot

```typescript
class Spot {
    // The SpotNode constructor path is internal. Public code obtains Spot
    // handles through SpotNode factories.
    requestTimeout: number;      // get / set (ms)
    publish(serviceName: string, topic: string): SendOp;
    sendChannel(channelName: string): SendOp;
    requestChannel(channelName: string): RequestOp;
    /** @throws {ConfigError} */
    setSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    unsetSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    subscriptionAt(index: number): SubscriptionEntry | null;
    /** @throws {RecvError} */
    subscribe(flags?: RecvFlags): TopicMessage | null;
    /** @throws {RecvError} */
    receiveSubscriptionEvent(flags?: RecvFlags): SubscriptionEvent | null;
    /** @throws {HandlerError} */
    onSendReady(handler: SpotSendReadyHandler): void;

    // --- routed send / request / reply builders ---
    sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOp;
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOp;
    requestToRouter(peerRid: RoutingId): RequestOp;
    replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                requestSeq: bigint): ReplyOp;
    replyToRouter(peerRid: RoutingId, requestSeq: bigint): ReplyOp;

    // --- routed receive ---
    /** @throws {RecvError} */
    recvRouted(flags?: RecvFlags): Received | null;
    /** @throws {HandlerError} */
    onRoutedReceive(handler: SpotRoutedHandler): void;
    /** @throws {HandlerError} */
    onDispatchEvent(handler: SpotDispatchEventHandler): void;
    /** @throws {RecvError} */
    recvActorJoin(flags?: RecvFlags): ActorJoinRequest | null;
    /** @throws {SubmitError} */
    replyActorJoin(request: ActorJoinRequest, accepted: boolean,
                   message: MessageLike): void;
    /** @throws {ConfigError} */
    actorsSnapshot(): ActorRef[];
    // --- identity / routing ---
    /**
     * Spot's logical address / routed ownership key.
     * Maps to zlink_set_routing_id(spot, ...) / zlink_get_routing_id(spot, ...).
     */
    setRoutingId(rid: RoutingId): void;
    readonly routingId: RoutingId;

    /** @throws {CloseError} */
    close(): void;
}

interface SendOp {
    message(message: MessageLike): SendSubmitOp;
}

interface SendSubmitOp {
    message(message: MessageLike): SendSubmitOp;
    flags(flags: SendFlags): SendSubmitOp;
    /** @throws {SubmitError} */
    submit(): boolean;
}

interface RequestOp {
    message(message: MessageLike): RequestSubmitOp;
}

interface RequestSubmitOp {
    message(message: MessageLike): RequestSubmitOp;
    timeout(timeoutMs: number): RequestSubmitOp;
    flags(flags: SendFlags): RequestCallbackSubmitOp;
    /** Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    submitAsync(): Promise<Message[]>;
    /** @throws {SubmitError} on submit failure other than temporary backpressure. */
    submit(callback: RequestCallback): boolean;
}

interface RequestCallbackSubmitOp {
    message(message: MessageLike): RequestCallbackSubmitOp;
    timeout(timeoutMs: number): RequestCallbackSubmitOp;
    flags(flags: SendFlags): RequestCallbackSubmitOp;
    /** @throws {SubmitError} on submit failure other than temporary backpressure. */
    submit(callback: RequestCallback): boolean;
}

interface ReplyOp {
    message(message: MessageLike): ReplySubmitOp;
}

interface ReplySubmitOp {
    message(message: MessageLike): ReplySubmitOp;
    flags(flags: SendFlags): ReplySubmitOp;
    /** @throws {SubmitError} */
    submit(): void;
}
```

`SendOp`, `RequestOp`, and `ReplyOp` are fluent operation builders. TypeScript
declarations must hide submit methods until at least one `message(...)` has
been added. JavaScript runtime code must perform the same validation and throw
a validation error for submit without payload. Repeated `message(...)` calls
append multipart payload parts in order. Async request submission uses
`submitAsync()` and has no submit flags; callback submission may use
`flags(...)` before `submit(callback)`.

`onDispatchEvent(...)` is the canonical SPOT readable-notification surface.
For `SUBSCRIBE_READABLE` and `ROUTED_READABLE`, callers must keep draining
`subscribe(...)` / `recvRouted(...)` until the binding reports no data /
`EAGAIN`.

### RegistryQueryClient

```typescript
class RegistryQueryClient {
    constructor(ctx: Context);
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConfigError} */
    snapshot(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
    /** @throws {CloseError} */
    close(): void;
}
```

### Service-Layer Entry Types

Value objects returned by `Registry`, `Discovery`, `SpotNode`, and
`RegistryQueryClient` snapshot / query calls. All fields are `readonly`
and every routing-id field is a `RoutingId` wrapper (never a raw
`Buffer`).

Service-layer enum surfaces use typed constants instead of raw `number`
fields in public entry and filter objects:

```typescript
const ServiceRole = {
    Invalid: 0,
    Spot: 2,
    Router: 3,
    Dealer: 4,
    Pub: 5,
    Sub: 6,
} as const;
type ServiceRoleValue = typeof ServiceRole[keyof typeof ServiceRole];

const ServiceKind = {
    Discovery: 1,
    SpotSub: 3,
    SpotPub: 4,
    Socket: 5,
} as const;
type ServiceKindValue = typeof ServiceKind[keyof typeof ServiceKind];

const SpotRole = {
    Pub: 1,
    Sub: 2,
} as const;
type SpotRoleValue = typeof SpotRole[keyof typeof SpotRole];

const SpotNodeState = {
    Idle: 1,
    Connecting: 2,
    PartialReady: 3,
    Ready: 4,
    Error: 5,
} as const;
type SpotNodeStateValue = typeof SpotNodeState[keyof typeof SpotNodeState];

const SocketType = {
    Any: 0,
    Pair: 0x1001,
    Pub: 0x1002,
    Sub: 0x1003,
    Dealer: 0x1004,
    Router: 0x1005,
    XPub: 0x1006,
    XSub: 0x1007,
    Stream: 0x1008,
} as const;
type SocketTypeValue = typeof SocketType[keyof typeof SocketType];

const SpotPeerSource = {
    Manual: 1,
    Discovery: 2,
    Mixed: 3,
} as const;
type SpotPeerSourceValue = typeof SpotPeerSource[keyof typeof SpotPeerSource];

const SpotPeerState = {
    Configured: 1,
    Connecting: 2,
    Connected: 3,
} as const;
type SpotPeerStateValue = typeof SpotPeerState[keyof typeof SpotPeerState];

const RegistryState = {
    Idle: 1,
    Active: 2,
    Degraded: 3,
    Error: 4,
} as const;
type RegistryStateValue = typeof RegistryState[keyof typeof RegistryState];

const TopologySource = {
    Manual: 1,
    Discovery: 2,
    Registry: 3,
} as const;
type TopologySourceValue = typeof TopologySource[keyof typeof TopologySource];

const TopologyState = {
    Discovered: 1,
    Connecting: 2,
    Ready: 3,
    Lost: 4,
    Error: 5,
    Stopped: 6,
} as const;
type TopologyStateValue = typeof TopologyState[keyof typeof TopologyState];

const SpotNodeSocketOwner = {
    Any: 0,
    Node: 1,
    Spot: 2,
} as const;
type SpotNodeSocketOwnerValue =
    typeof SpotNodeSocketOwner[keyof typeof SpotNodeSocketOwner];
```

Primary entry types used in the default service flow:

```typescript
/** Discovery / Registry member peer entry. */
interface MemberPeerEntry {
    readonly autoConnectType: AutoConnectType;
    readonly serviceRole: ServiceRoleValue;
    readonly channelName: string;
    readonly endpoint: string;
    readonly routingId: RoutingId;
    readonly value: bigint;                  // int64 user value
    readonly weight: number;                 // uint32, 0..100
}

/** Registry topology entry (full topology view). */
interface RegistryTopologyEntry {
    readonly autoConnectType: AutoConnectType;
    readonly routingId: RoutingId;
    readonly serviceKind: ServiceKindValue;
    readonly serviceRole: ServiceRoleValue;
    readonly channelName: string;
    readonly endpoint: string;
    readonly source: TopologySourceValue;
    readonly state: TopologyStateValue;
    readonly desiredCount: number;           // uint32
    readonly readyCount: number;             // uint32
    readonly errorCode: number;              // uint32 errno
    readonly lastReportedMs: bigint;         // uint64 epoch ms
}

/** SpotNode runtime status snapshot. */
interface SpotNodeStatus {
    readonly channelName: string;
    readonly localEndpoint: string;
    readonly nodeRoutingId: RoutingId;
    readonly state: SpotNodeStateValue;
    readonly configuredPeerCount: number;    // uint32
    readonly activePeerCount: number;        // uint32
    readonly connectedPeerCount: number;     // uint32
    readonly subjectCount: number;           // uint32
    readonly readySubjectCount: number;      // uint32
    readonly disconnectedSubTargetCount: number;     // uint32
    readonly disconnectedRoutedTargetCount: number;  // uint32
    readonly lastError: number;              // int32
    readonly lastChangedMs: bigint;          // uint64 epoch ms
}
```

Advanced / Diagnostic entry types and filters:

```typescript
/** Registry service summary roll-up entry. */
interface RegistryServiceSummaryEntry {
    readonly autoConnectType: AutoConnectType;
    readonly serviceRole: ServiceRoleValue;
    readonly channelName: string;
    readonly totalCount: number;             // uint32
    readonly connectingCount: number;        // uint32
    readonly readyCount: number;             // uint32
    readonly errorCount: number;             // uint32
    readonly stoppedCount: number;           // uint32
    readonly lastReportedMs: bigint;         // uint64 epoch ms
}

/** Registry status snapshot. */
interface RegistryStatus {
    readonly registryId: number;             // uint32
    readonly bindEndpoint: string;
    readonly state: RegistryStateValue;
    readonly topologyEntryCount: number;     // uint32
    readonly peerRegistryCount: number;      // uint32
    readonly connectedPeerRegistryCount: number; // uint32
    readonly listSeq: bigint;                // uint64
    readonly lastError: number;              // int32
    readonly lastChangedMs: bigint;          // uint64 epoch ms
}

/** SpotNode peer entry. */
interface SpotNodePeerEntry {
    readonly channelName: string;
    readonly localEndpoint: string;
    readonly peerEndpoint: string;
    readonly source: SpotPeerSourceValue;
    readonly state: SpotPeerStateValue;
    readonly weight: number;                 // uint32, 0..100
    readonly connectedSinceMs: bigint;       // uint64 epoch ms
    readonly lastChangedMs: bigint;          // uint64 epoch ms
}

/** SpotNode subject entry. */
interface SpotNodeSubjectEntry {
    readonly role: SpotRoleValue;
    readonly subject: string;
    readonly subjectKind: number;            // uint32
    readonly readyPeerCount: number;         // uint32
    readonly activePeerCount: number;        // uint32
    readonly lastChangedMs: bigint;          // uint64 epoch ms
}

/** SpotNode Spot snapshot entry. */
interface SpotNodeSpotEntry {
    readonly spotRid: RoutingId;
    readonly dispatchHandlerAttached: boolean;
    readonly joinedActorCount: number;
    readonly pendingActorJoinCount: number;
    readonly routeSynced: boolean;
    readonly lastChangedMs: bigint;
}

/** SpotNode Actor snapshot entry. */
interface SpotNodeActorEntry {
    readonly actor: ActorRef;
    readonly joined: boolean;
    readonly joinedSpotRid: RoutingId | null;
    readonly routeSynced: boolean;
    readonly pendingMessageCount: number;
    readonly lastChangedMs: bigint;
}

/** Filter for Registry service summary snapshot. */
interface RegistryServiceSummaryFilter {
    readonly autoConnectType?: AutoConnectType;
    readonly serviceRole?: ServiceRoleValue;
    readonly channelName?: string;
}

/** Filter for Registry topology snapshot / query. */
interface RegistryTopologyFilter {
    readonly autoConnectType?: AutoConnectType;
    readonly serviceKind?: ServiceKindValue;
    readonly serviceRole?: ServiceRoleValue;
    readonly channelName?: string;
    readonly routingId?: RoutingId;
    readonly state?: TopologyStateValue;
    readonly source?: TopologySourceValue;
}

/** Filter for SpotNode peers query. */
interface SpotNodePeerFilter {
    readonly peerEndpoint?: string;
    readonly source?: SpotPeerSourceValue;
    readonly state?: SpotPeerStateValue;
}

/** Filter for SpotNode subjects query. */
interface SpotNodeSubjectFilter {
    readonly role?: SpotRoleValue;
    readonly subject?: string;
    readonly subjectKind?: number;
}

/** Filter for SpotNode internal socket snapshots. */
interface SpotNodeSocketSnapshotFilter {
    readonly owner?: SpotNodeSocketOwnerValue;
    readonly socketType?: SocketTypeValue;
    readonly socketName?: string;
}

/** SpotNode internal socket diagnostic snapshot entry. */
interface SpotNodeSocketSnapshotEntry {
    readonly owner: SpotNodeSocketOwnerValue;
    readonly ownerId: bigint;
    readonly ownerName: string;
    readonly socketName: string;
    readonly socketType: SocketTypeValue;
    readonly autoHwmVisible: boolean;
    readonly snapshot: MonitorSnapshot;
}
```

---

## Poller

```typescript
const PollEventFlag = {
    PollIn: 1,
    PollOut: 2,
    PollErr: 4,
    PollPri: 8,
} as const;
type PollEventFlagValue =
    typeof PollEventFlag[keyof typeof PollEventFlag];

class Poller {
    constructor();

    // --- socket registration ---
    /** @throws {ConfigError} */
    add(socket: BaseSocket,
        events: readonly PollEventFlagValue[],
        tag?: any): void;
    /** @throws {ConfigError} */
    modify(socket: BaseSocket,
           events: readonly PollEventFlagValue[]): void;
    /** @throws {ConfigError} */
    remove(socket: BaseSocket): boolean;

    // --- file descriptor registration ---
    /** @throws {ConfigError} */
    addFd(fd: number, events: readonly PollEventFlagValue[],
          tag?: any): void;
    /** @throws {ConfigError} */
    modifyFd(fd: number, events: readonly PollEventFlagValue[]): void;
    /** @throws {ConfigError} */
    removeFd(fd: number): boolean;

    // --- timer registration ---
    /** @throws {ConfigError} */
    add(timer: Timer, tag?: any): void;
    /** @throws {ConfigError} */
    remove(timer: Timer): boolean;

    // --- poll ---
    /** Number of registered pollable items. Maps to zlink_poller_size. */
    readonly size: number;
    /** @throws {RecvError} */
    wait(timeoutMs: number): PollEvent | null;
    /** @throws {RecvError} */
    waitAll(maxEvents: number, timeoutMs: number): PollEvent[];
    /** @throws {CloseError} */
    destroy(): void;
    /** @throws {CloseError} */
    close(): void;
}

interface PollEvent {
    socket: BaseSocket | null;
    fd: number | null;
    timer: Timer | null;
    tag: any;
    events: readonly PollEventFlagValue[];
    revents: readonly PollEventFlagValue[];
}
```

The current public poller contract is still generic. It does not yet expose a
Spot-aware result carrying owner `Spot`, dispatch event kind, and drain
subject together.
`PollEventFlag.PollOut` is a send-recovery readiness signal shared with
`onSendReady(...)`, not a transport-writable bit.

---

## Timer

### Timer

```typescript
class Timer {
    constructor();

    /** @throws {ConfigError} */
    static fromSpot(spot: Spot): Timer;

    /** @throws {ConfigError} */
    start(intervalNs: bigint, repeatCount: bigint): void;
    /** @throws {ConfigError} */
    stop(): void;
    /** @throws {RecvError} */
    recv(): bigint | null;
    /** @throws {HandlerError} */
    onFire(handler: TimerHandler): void;
    /** @throws {CloseError} */
    close(): void;
}
```

---

## Callback Types

```typescript
type SocketSendReadyHandler = () => void;
type SocketMonitorHandler = (event: MonitorEvent) => void;
type StreamPacketHandler = (sourceRid: RoutingId,
                            header: Message,
                            body: Message) => void;
type SpotSendReadyHandler = () => void;
type SpotRoutedHandler = (message: Received) => void;
const SpotDispatchEvent = {
  SubscribeReadable: 1,
  RoutedReadable: 2,
  TimerReadable: 3,
  ChannelReplyReadable: 4,
  ActorReadable: 5,
  ActorJoinReadable: 6,
} as const;
type SpotDispatchEvent =
    typeof SpotDispatchEvent[keyof typeof SpotDispatchEvent];

const SpotDispatchSubjectKind = {
  Spot: 1,
  Timer: 2,
  ChannelDealer: 3,
  Actor: 4,
} as const;
type SpotDispatchSubjectKind =
    typeof SpotDispatchSubjectKind[keyof typeof SpotDispatchSubjectKind];

interface SpotDispatchInfo {
  event: SpotDispatchEvent;
  subjectKind: SpotDispatchSubjectKind;
  timer: Timer | null;
  // ActorReadable carries a callback-lifetime ActorRef copy instead of a raw
  // native subject pointer.
  actorRef: ActorRef | null;
  recvActorPart(flags?: RecvFlags): ActorPart | null;
}
type ActorAdmissionHandler =
    (actorId: string, message: Message) => ActorAdmissionResult;
type SpotDispatchEventHandler = (info: SpotDispatchInfo) => void;
type RequestCallback = (result: RequestResult, parts: readonly Message[]) => void;
type TimerHandler = (timer: Timer, fireCount: bigint) => void;
```

`onDispatchEvent(...)` is the canonical SPOT readable-notification surface.
For `SUBSCRIBE_READABLE` and `ROUTED_READABLE`, callers must keep draining
`subscribe(...)` / `recvRouted(...)` until the binding reports no data /
`EAGAIN`.
For `ChannelReplyReadable`, request promises and callbacks progress their
replies inside the binding. The public API does not expose the native channel
dealer subject.
For `ActorReadable`, `actorRef` identifies the readable Actor and
the native Actor subject pointer is not part of the public Actor contract.

---

## Utilities

### Stopwatch

High-resolution stopwatch for measuring elapsed time.

```typescript
class Stopwatch {
    constructor();

    /// Return elapsed microseconds without stopping.
    intermediate(): number;

    /// Stop the stopwatch and return total elapsed microseconds.
    stop(): number;

    /** @throws {CloseError} */
    close(): void;
}
```

### Thread

Thin wrapper for the core native thread utility. This is an explicit blocking
utility and is not used by messaging hot paths.

```typescript
class Thread {
    constructor(handler: () => void);
    join(): void;
}
```

### AtomicCounter

Native atomic counter utility.

```typescript
class AtomicCounter {
    constructor(initialValue?: number);
    set(value: number): void;
    inc(): number;
    dec(): number;
    value(): number;
    close(): void;
}
```

## Peer Disconnect by Routing ID

Node bindings expose `socket.disconnectRid(routingId)` on connectable raw
sockets and `spotNode.disconnectPeerRid(targetNodeRid)` on `SpotNode`.
`StreamSocket` is bind-only and does not expose peer-rid disconnect. The
duplicate policy option and `NOT_FOUND` / `CONFLICT` / `BUSY` connect errors
mirror the C core. `Spot` does not expose a peer-rid disconnect method.
