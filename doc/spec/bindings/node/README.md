[Spec Index](../../README.md) · [Bindings Policy](../README.md)

# Node / TypeScript Binding Specification

This document defines the complete public API surface of the zlink Node/TypeScript
binding. Every class, method, and type listed here is part of the contract that
the binding must expose. Internal/private members are omitted.

---

## Core

### Context

```typescript
class Context {
    constructor();
    readonly options: ContextOptions;
    shutdown(): void;
    close(): void;
}
```

### ContextOptions

```typescript
class ContextOptions {
    ioThreads: number;          // get / set
    maxSockets: number;         // get / set
    readonly socketLimit: number;
    maxMsgSize: number;         // get / set
    readonly msgTSize: number;
    threadPriority: number;     // get / set
    threadSchedulingPolicy: number; // get / set
    blocky: boolean;            // get / set
    addThreadAffinity(cpu: number): void;
    removeThreadAffinity(cpu: number): void;
}
```

### Module-Level

```typescript
function version(): [number, number, number];

/// Return the errno for the current thread.
function errno(): number;

/// Return a human-readable string for the given error number.
function strerror(code: number): string;

/// Check if the library supports a given capability (e.g. "ipc", "tls").
function has(capability: string): boolean;

/// Start a built-in proxy between frontend and backend sockets.
function proxy(frontend: BaseSocket, backend: BaseSocket,
               capture?: BaseSocket): void;

/// Start a steerable proxy with an additional control socket.
function proxySteerable(frontend: BaseSocket, backend: BaseSocket,
                        capture: BaseSocket | null,
                        control: BaseSocket): void;

/// Sleep for the given number of seconds.
function sleep(seconds: number): void;

/// Close all parts in a multipart message array.
function multipartClose(parts: Message[]): void;
```

---

## Socket Types

All sockets expose `bind()`, `unbind()`, and `close()` from `BaseSocket`.
Connectable sockets also expose `connect()` and `disconnect()`.

### PairSocket

```typescript
class PairSocket {
    constructor(ctx: Context);
    readonly options: CommonSocketOptions;
    bind(endpoint: string): void;
    unbind(endpoint: string): void;
    connect(endpoint: string): void;
    disconnect(endpoint: string): void;
    send(message: MessageLike, flags?: SendFlags): void;
    send(parts: readonly MessageLike[], flags?: SendFlags): void;
    recv(flags?: RecvFlags): Received;
    onReceive(handler: SocketRecvHandler): void;
    onSendReady(handler: SocketSendReadyHandler): void;
    close(): void;
}
```

### PubSocket

```typescript
class PubSocket {
    constructor(ctx: Context);
    readonly options: PubSocketOptions;
    bind(endpoint: string): void;
    unbind(endpoint: string): void;
    connect(endpoint: string): void;
    disconnect(endpoint: string): void;
    publish(topic: string, message: MessageLike, flags?: SendFlags): void;
    publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): void;
    onSendReady(handler: SocketSendReadyHandler): void;
    attachDiscovery(discovery: Discovery): void;
    close(): void;
}
```

### SubSocket

```typescript
class SubSocket {
    constructor(ctx: Context);
    readonly options: SubSocketOptions;
    bind(endpoint: string): void;
    unbind(endpoint: string): void;
    connect(endpoint: string): void;
    disconnect(endpoint: string): void;
    setSubscription(topicOrPattern: string): void;
    unsetSubscription(topicOrPattern: string): void;
    subscribe(flags?: RecvFlags): Subscribed;
    onSubscribe(handler: SocketSubscribeHandler): void;
    attachDiscovery(discovery: Discovery): void;
    close(): void;
}
```

### DealerSocket

```typescript
class DealerSocket {
    constructor(ctx: Context);
    readonly options: DealerSocketOptions;
    bind(endpoint: string): void;
    unbind(endpoint: string): void;
    connect(endpoint: string): void;
    disconnect(endpoint: string): void;
    setRoutingId(routingId: BufferLike): void;
    getRoutingId(): Buffer;
    send(message: MessageLike, flags?: SendFlags): void;
    send(parts: readonly MessageLike[], flags?: SendFlags): void;
    recv(flags?: RecvFlags): Received;
    onReceive(handler: SocketRecvHandler): void;
    onSendReady(handler: SocketSendReadyHandler): void;
    attachDiscovery(discovery: Discovery): void;
    close(): void;
}
```

### RouterSocket

```typescript
class RouterSocket {
    constructor(ctx: Context);
    readonly options: RouterSocketOptions;
    bind(endpoint: string): void;
    unbind(endpoint: string): void;
    connect(endpoint: string): void;
    disconnect(endpoint: string): void;
    setRoutingId(routingId: BufferLike): void;
    getRoutingId(): Buffer;
    send(routingId: BufferLike, message: MessageLike, flags?: SendFlags): void;
    send(routingId: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;
    recv(flags?: RecvFlags): Received;
    onReceive(handler: SocketRecvHandler): void;
    onSendReady(handler: SocketSendReadyHandler): void;
    attachDiscovery(discovery: Discovery): void;

    // --- router → spot routed send ---
    sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
               message: MessageLike, flags?: SendFlags): void;
    sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
               parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- router → spot routed request (async) — no flags ---
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  message: MessageLike, timeout?: number): Promise<Received>;
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  parts: readonly MessageLike[], timeout?: number): Promise<Received>;

    // --- router → spot routed request (callback) — throws on submit failure ---
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  message: MessageLike,
                  callback: RequestResultCallback,
                  flags?: SendFlags, timeout?: number): void;
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  parts: readonly MessageLike[],
                  callback: RequestResultCallback,
                  flags?: SendFlags, timeout?: number): void;

    // --- router → spot routed reply ---
    replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
    replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- router spot receive ---
    recvSpot(flags?: RecvFlags): Received;
    onSpotReceive(handler: RouterSpotHandler): void;

    close(): void;
}
```

### XPubSocket

```typescript
class XPubSocket {
    constructor(ctx: Context);
    readonly options: PubSocketOptions;
    bind(endpoint: string): void;
    unbind(endpoint: string): void;
    connect(endpoint: string): void;
    disconnect(endpoint: string): void;
    publish(topic: string, message: MessageLike, flags?: SendFlags): void;
    publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): void;
    receiveSubscriptionEvent(flags?: RecvFlags): SubscriptionEvent;
    onSendReady(handler: SocketSendReadyHandler): void;
    close(): void;
}
```

### XSubSocket

```typescript
class XSubSocket {
    constructor(ctx: Context);
    readonly options: SubSocketOptions;
    bind(endpoint: string): void;
    unbind(endpoint: string): void;
    connect(endpoint: string): void;
    disconnect(endpoint: string): void;
    setSubscription(topicOrPattern: string): void;
    unsetSubscription(topicOrPattern: string): void;
    subscribe(flags?: RecvFlags): Subscribed;
    onSubscribe(handler: SocketSubscribeHandler): void;
    close(): void;
}
```

### StreamSocket

```typescript
class StreamSocket {
    constructor(ctx: Context);
    readonly options: StreamSocketOptions;
    bind(endpoint: string): void;
    unbind(endpoint: string): void;
    setRoutingId(routingId: BufferLike): void;
    getRoutingId(): Buffer;
    send(routingId: BufferLike, message: MessageLike, flags?: SendFlags): void;
    send(routingId: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;
    recv(flags?: RecvFlags): Received;
    onReceive(handler: SocketRecvHandler): void;
    onSendReady(handler: SocketSendReadyHandler): void;
    close(): void;
}
```

### Socket Option Classes

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
}

class DealerSocketOptions extends CommonSocketOptions {
    probe: boolean;              // set only
}

class RouterSocketOptions extends CommonSocketOptions {
    mandatory: boolean;          // get / set
    handover: boolean;           // get / set
    probe: boolean;              // get / set
    connectRoutingId: BufferLike;// set only
}

class PubSocketOptions extends CommonSocketOptions {
    verbose: boolean;            // set only
    verboser: boolean;           // set only
    noDrop: boolean;             // set only
    manual: boolean;             // set only
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
    constructor(data: Buffer);
    static from(data: BufferLike): Message;
    data(): Buffer;
    size(): number;
    getProperty(name: string): string | null;
    refCount(): number;
    close(): void;
}

type MessageLike = Message | Buffer | Uint8Array | string;
type BufferLike = Buffer | Uint8Array | string;
```

### RoutingId

Routing IDs are represented as `Buffer` values (1-255 bytes).

### Received

```typescript
class Received {
    readonly routingId: Buffer | null;
    readonly parts: Message[];
    readonly requestSeq: bigint | null;
    toBytesList(): Buffer[];
    close(): void;
}
```

### Subscribed

```typescript
class Subscribed {
    readonly routingId: Buffer | null;
    readonly topic: string;
    readonly parts: Message[];
    toBytesList(): Buffer[];
    close(): void;
}
```

### SubscriptionEvent

```typescript
class SubscriptionEvent {
    readonly routingId: Buffer | null;
    readonly topic: string;
    readonly subscribed: boolean;
}
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

### SubmitResult

Result codes for send/request/reply/publish operations.
All failures throw `ZlinkError` with `.code` indicating the failure code.

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
} as const;

type RecvResult = typeof RecvResult[keyof typeof RecvResult];
```

### HandlerResult

Result codes for handler registration operations (onReceive, onSubscribe, etc.).

```typescript
const HandlerResult = {
    Ok: 0,
    InvalidArgument: 301,
    Busy: 302,
    NotSupported: 303,
    Deadlock: 304,
    InvalidHandle: 305,
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
} as const;

type ConfigResult = typeof ConfigResult[keyof typeof ConfigResult];
```

### ZlinkError

Exception thrown when any operation fails.
The `code` field is a globally unique `int` that spans all result enum
ranges (0-703). The code alone identifies the error without needing to
know which enum it belongs to.

```typescript
class ZlinkError extends Error {
    readonly code: number;
    readonly errno: number;
    constructor(code: number, errno?: number);
}
```

---

## Request-Reply

### RequestDealer

```typescript
class RequestDealer {
    constructor(socket: DealerSocket);
    socket(): DealerSocket;

    // Promise (async) — no flags, timeout = 0 uses socket default
    request(message: MessageLike, timeout?: number): Promise<Received>;
    request(parts: readonly MessageLike[], timeout?: number): Promise<Received>;

    // Callback — throws on submit failure, timeout = 0 uses socket default
    request(message: MessageLike,
            callback: RequestResultCallback,
            flags?: SendFlags, timeout?: number): void;
    request(parts: readonly MessageLike[],
            callback: RequestResultCallback,
            flags?: SendFlags, timeout?: number): void;

    recv(flags?: RecvFlags): Received;
    onReceive(handler: (received: Received) => void): void;
    close(): void;
}
```

### RequestRouter

```typescript
class RequestRouter {
    constructor(socket: RouterSocket);
    socket(): RouterSocket;

    // Promise (async) — no flags, timeout = 0 uses socket default
    request(routingId: BufferLike, message: MessageLike,
            timeout?: number): Promise<Received>;
    request(routingId: BufferLike, parts: readonly MessageLike[],
            timeout?: number): Promise<Received>;

    // Callback — throws on submit failure, timeout = 0 uses socket default
    request(routingId: BufferLike, message: MessageLike,
            callback: RequestResultCallback,
            flags?: SendFlags, timeout?: number): void;
    request(routingId: BufferLike, parts: readonly MessageLike[],
            callback: RequestResultCallback,
            flags?: SendFlags, timeout?: number): void;

    reply(routingId: BufferLike, requestSeq: bigint, message: MessageLike,
          flags?: SendFlags): void;
    reply(routingId: BufferLike, requestSeq: bigint, parts: readonly MessageLike[],
          flags?: SendFlags): void;

    recv(flags?: RecvFlags): Received;
    onReceive(handler: (received: Received) => void): void;
    close(): void;
}
```

---

## Monitoring

### MonitorSocket

```typescript
class MonitorSocket {
    recv(): SocketMonitorEventValue;
    onEvent(handler: (event: SocketMonitorEventValue) => void): void;
    snapshot(): MonitorSnapshot;
    close(): void;
}

interface SocketMonitorEventValue {
    event: number;
    value: number;
    local: string;
    remote: string;
}
```

### ServiceMonitor

```typescript
class ServiceMonitor {
    recv(): ServiceEvent;
    onEvent(handler: (event: ServiceEvent) => void): void;
    snapshot(): MonitorSnapshot;
    close(): void;
}
```

### MonitorSnapshot

```typescript
interface MonitorSnapshot {
    sourceKind: number;
    stateFlags: number;
    detailFlags: number;
    sndPendingMsgs: number;
    rcvPendingMsgs: number;
}
```

### ServiceEvent

```typescript
class ServiceEvent {
    readonly serviceKind: number;
    readonly eventType: number;
    readonly status: number;
    readonly errorCode: number;
    readonly value: number;
    readonly detailFlags: number;
    readonly serviceName: string;
    readonly endpoint: string;
    readonly routingId: Buffer | null;
    readonly subject: string;
    readonly subjectKind: number;
}
```

---

## Services

### Registry

```typescript
class Registry {
    constructor(ctx: Context);
    bind(pubEndpoint: string, routerEndpoint: string): void;
    setId(id: number): void;
    addPeer(pubEndpoint: string): void;
    setHeartbeat(intervalMs: number, timeoutMs: number): void;
    setBroadcastInterval(intervalMs: number): void;
    setTlsServer(cert: string, key: string, requireClient?: number): void;
    setTlsClient(ca: string, host: string, trust?: number): void;
    statusSnapshot(): RegistryStatus;
    serviceSummarySnapshot(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[];
    topologySnapshot(): RegistryTopologyEntry[];
    topologyQuery(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
    memberPeers(serviceType: number, serviceName?: string): MemberPeerEntry[];
    memberPeerMetadata(serviceType: number, serviceName: string,
                       serviceRole: number, endpoint: string): Buffer;
    close(): void;
}
```

### Discovery

```typescript
class Discovery {
    constructor(ctx: Context, serviceType: number, serviceName: string);
    readonly serviceType: number;
    readonly serviceName: string;
    connectRegistry(endpoint: string): void;
    setValue(value: number): void;
    getValue(): number;
    setMetadata(metadata: BufferLike | string): void;
    getMetadata(): Buffer;
    memberPeers(): MemberPeerEntry[];
    memberPeerMetadata(serviceRole: number, endpoint: string): Buffer;
    monitorOpen(events?: ServiceMonitorEventMask): ServiceMonitor;
    setTlsClient(ca: string, host: string, trust?: number): void;
    close(): void;
}
```

### SpotNode

```typescript
class SpotNode {
    constructor(ctx: Context);
    bind(endpoint: string): void;
    connectPeer(endpoint: string): void;
    disconnectPeer(endpoint: string): void;
    attachDiscovery(discovery: Discovery): void;
    setTlsServer(cert: string, key: string, requireClient?: number): void;
    setTlsClient(ca: string, host: string, trust?: number): void;
    statusSnapshot(): SpotNodeStatus;
    peersSnapshot(): SpotNodePeerEntry[];
    peersQuery(filter?: SpotNodePeerFilter): SpotNodePeerEntry[];
    subjectsSnapshot(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[];
    close(): void;
}
```

### Spot

```typescript
class Spot {
    constructor(node: SpotNode);
    publish(topic: string, payload: MessageLike, flags?: SendFlags): void;
    publish(topic: string, payloadParts: readonly MessageLike[], flags?: SendFlags): void;
    setSubscription(topicOrPattern: string): void;
    unsetSubscription(topicOrPattern: string): void;
    subscribe(flags?: RecvFlags): Subscribed;
    onSubscribe(handler: SpotSubHandler): void;
    onSendReady(handler: SpotSendReadyHandler): void;
    setLinger(milliseconds: number): void;
    setSendHighWaterMark(value: number): void;
    setReceiveHighWaterMark(value: number): void;
    setSendTimeout(milliseconds: number): void;
    setReceiveTimeout(milliseconds: number): void;
    setNoDrop(enabled: boolean): void;

    // --- routed send (spot → spot) ---
    sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
               message: MessageLike, flags?: SendFlags): void;
    sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
               parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- routed request (spot → spot, async) — no flags ---
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  message: MessageLike, timeout?: number): Promise<Received>;
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  parts: readonly MessageLike[], timeout?: number): Promise<Received>;

    // --- routed request (spot → spot, callback) — throws on submit failure ---
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  message: MessageLike,
                  callback: RequestResultCallback,
                  flags?: SendFlags, timeout?: number): void;
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  parts: readonly MessageLike[],
                  callback: RequestResultCallback,
                  flags?: SendFlags, timeout?: number): void;

    // --- routed reply (spot → spot) ---
    replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
    replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- routed send (spot → router) ---
    sendToRouter(peerRid: BufferLike, message: MessageLike, flags?: SendFlags): void;
    sendToRouter(peerRid: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- routed request (spot → router, async) — no flags ---
    requestToRouter(peerRid: BufferLike, message: MessageLike,
                    timeout?: number): Promise<Received>;
    requestToRouter(peerRid: BufferLike, parts: readonly MessageLike[],
                    timeout?: number): Promise<Received>;

    // --- routed request (spot → router, callback) — throws on submit failure ---
    requestToRouter(peerRid: BufferLike, message: MessageLike,
                    callback: RequestResultCallback,
                    flags?: SendFlags, timeout?: number): void;
    requestToRouter(peerRid: BufferLike, parts: readonly MessageLike[],
                    callback: RequestResultCallback,
                    flags?: SendFlags, timeout?: number): void;

    // --- routed reply (spot → router) ---
    replyToRouter(peerRid: BufferLike, requestSeq: bigint,
                  message: MessageLike, flags?: SendFlags): void;
    replyToRouter(peerRid: BufferLike, requestSeq: bigint,
                  parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- routed receive ---
    recvRouted(flags?: RecvFlags): Received;
    onRoutedReceive(handler: SpotRoutedHandler): void;
    onDispatchEvent(handler: SpotDispatchEventHandler): void;

    close(): void;
}
```

### RegistryQueryClient

```typescript
class RegistryQueryClient {
    constructor(ctx: Context);
    connect(endpoint: string): void;
    snapshot(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
    close(): void;
}
```

---

## Poller

```typescript
class Poller {
    constructor();

    // --- socket registration ---
    addSocket(socket: BaseSocket, events: number, userData?: any): void;
    modifySocket(socket: BaseSocket, events: number): void;
    removeSocket(socket: BaseSocket): void;

    // --- file descriptor registration ---
    addFd(fd: number, events: number, userData?: any): void;
    modifyFd(fd: number, events: number): void;
    removeFd(fd: number): void;

    // --- timer registration ---
    addTimer(timer: Timer, userData?: any): void;
    removeTimer(timer: Timer): void;

    // --- poll ---
    size(): number;
    wait(timeoutMs: number): PollerEvent | null;
    waitAll(events: number, timeoutMs: number): PollerEvent[];
    poll(timeoutMs: number): number[];

    destroy(): void;
    close(): void;
}

interface PollerEvent {
    sourceKind: number;
    socket: BaseSocket | null;
    fd: number;
    timer: Timer | null;
    userData: any;
    events: number;
}
```

---

## Timer

### Timer

```typescript
class Timer {
    constructor();

    static fromSpot(spot: Spot): Timer;

    start(intervalNs: bigint, repeatCount: bigint): void;
    stop(): void;
    recv(flags?: number): bigint;
    onFire(handler: TimerHandler): void;
    close(): void;
}
```

---

## Callback Types

```typescript
type SocketRecvHandler = (routingId: Buffer | null, parts: Message[]) => void;
type SocketSubscribeHandler = (routingId: Buffer | null, topic: string, parts: Message[]) => void;
type SocketSendReadyHandler = () => void;
type SpotSubHandler = SocketSubscribeHandler;
type SpotSendReadyHandler = () => void;
type SpotRoutedHandler = (sourceRid: Buffer | null, spotRid: Buffer | null,
                          requestSeq: bigint, parts: Message[]) => void;
type SpotDispatchEventHandler = (event: number) => void;
type RouterSpotHandler = (sourceNodeRid: Buffer | null, sourceSpotRid: Buffer | null,
                          requestSeq: bigint, parts: Message[]) => void;
type RequestResultCallback = (result: RequestResult, reply?: Received) => void;
type TimerHandler = (timer: Timer, fireCount: bigint) => void;
type ServiceMonitorHandler = (event: ServiceEvent) => void;
type ServiceMonitorEventMask = number;
```

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

    close(): void;
}
```

### Thread

Not wrapped: Node.js is single-threaded. Use native `worker_threads`
for parallelism and `setTimeout`/`setInterval` for scheduling.

### AtomicCounter

Not wrapped: Node.js is single-threaded; concurrent atomic operations
are not applicable. Use a plain variable or `SharedArrayBuffer` with
`Atomics` in worker-thread scenarios.
