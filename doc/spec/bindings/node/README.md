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
    send(message: MessageLike): void;
    send(parts: readonly MessageLike[]): void;
    trySend(message: MessageLike): SendResult;
    trySend(parts: readonly MessageLike[]): SendResult;
    recv(): Received;
    tryRecv(): Received | null;
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
    publish(topic: string, message: MessageLike): void;
    publish(topic: string, parts: readonly MessageLike[]): void;
    tryPublish(topic: string, message: MessageLike): SendResult;
    tryPublish(topic: string, parts: readonly MessageLike[]): SendResult;
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
    subscribe(): Subscribed;
    trySubscribe(): Subscribed | null;
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
    send(message: MessageLike): void;
    send(parts: readonly MessageLike[]): void;
    trySend(message: MessageLike): SendResult;
    trySend(parts: readonly MessageLike[]): SendResult;
    recv(): Received;
    tryRecv(): Received | null;
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
    send(routingId: BufferLike, message: MessageLike): void;
    send(routingId: BufferLike, parts: readonly MessageLike[]): void;
    trySend(routingId: BufferLike, message: MessageLike): SendResult;
    trySend(routingId: BufferLike, parts: readonly MessageLike[]): SendResult;
    recv(): Received;
    tryRecv(): Received | null;
    onReceive(handler: SocketRecvHandler): void;
    onSendReady(handler: SocketSendReadyHandler): void;
    attachDiscovery(discovery: Discovery): void;

    // --- router → spot routed send ---
    sendSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
             message: MessageLike): void;
    sendSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
             parts: readonly MessageLike[]): void;
    trySendSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                message: MessageLike): SendResult;
    trySendSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                parts: readonly MessageLike[]): SendResult;

    // --- router → spot routed request ---
    requestSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                message: MessageLike,
                options?: { timeout?: number }): Promise<Received>;
    requestSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                parts: readonly MessageLike[],
                options?: { timeout?: number }): Promise<Received>;
    requestSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                message: MessageLike,
                callback: (err: Error | null, reply?: Received) => void,
                options?: { timeout?: number }): void;

    // --- router → spot routed reply ---
    replySpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
              requestSeq: bigint, message: MessageLike): void;
    replySpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
              requestSeq: bigint, parts: readonly MessageLike[]): void;

    // --- router spot receive ---
    recvSpot(): Received;
    tryRecvSpot(): Received | null;
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
    publish(topic: string, message: MessageLike): void;
    publish(topic: string, parts: readonly MessageLike[]): void;
    tryPublish(topic: string, message: MessageLike): SendResult;
    tryPublish(topic: string, parts: readonly MessageLike[]): SendResult;
    receiveSubscriptionEvent(): SubscriptionEvent;
    tryReceiveSubscriptionEvent(): SubscriptionEvent | null;
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
    subscribe(): Subscribed;
    trySubscribe(): Subscribed | null;
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
    send(routingId: BufferLike, message: MessageLike): void;
    send(routingId: BufferLike, parts: readonly MessageLike[]): void;
    trySend(routingId: BufferLike, message: MessageLike): SendResult;
    trySend(routingId: BufferLike, parts: readonly MessageLike[]): SendResult;
    recv(): Received;
    tryRecv(): Received | null;
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

### SendResult

```typescript
const SendResult = {
    Sent: 0,
    Backpressured: 1,
    NotReady: 2,
} as const;

type SendResult = typeof SendResult[keyof typeof SendResult];
```

---

## Request-Reply

### RequestDealer

```typescript
class RequestDealer {
    constructor(socket: DealerSocket);
    socket(): DealerSocket;

    // Promise overloads
    request(message: MessageLike, options?: { timeout?: number }): Promise<Received>;
    request(parts: readonly MessageLike[], options?: { timeout?: number }): Promise<Received>;
    // Callback overloads
    request(message: MessageLike,
            callback: (err: Error | null, reply?: Received) => void,
            options?: { timeout?: number }): void;
    request(parts: readonly MessageLike[],
            callback: (err: Error | null, reply?: Received) => void,
            options?: { timeout?: number }): void;

    tryRequest(message: MessageLike, options?: { timeout?: number }): Promise<Received>;
    tryRequest(parts: readonly MessageLike[], options?: { timeout?: number }): Promise<Received>;
    tryRequest(message: MessageLike,
               callback: (err: Error | null, reply?: Received) => void,
               options?: { timeout?: number }): void;
    tryRequest(parts: readonly MessageLike[],
               callback: (err: Error | null, reply?: Received) => void,
               options?: { timeout?: number }): void;

    recv(): Received;
    tryRecv(): Received | null;
    onReceive(handler: (received: Received) => void): void;
    close(): void;
}
```

### RequestRouter

```typescript
class RequestRouter {
    constructor(socket: RouterSocket);
    socket(): RouterSocket;

    // Promise overloads
    request(routingId: BufferLike, message: MessageLike,
            options?: { timeout?: number }): Promise<Received>;
    request(routingId: BufferLike, parts: readonly MessageLike[],
            options?: { timeout?: number }): Promise<Received>;
    // Callback overloads
    request(routingId: BufferLike, message: MessageLike,
            callback: (err: Error | null, reply?: Received) => void,
            options?: { timeout?: number }): void;
    request(routingId: BufferLike, parts: readonly MessageLike[],
            callback: (err: Error | null, reply?: Received) => void,
            options?: { timeout?: number }): void;

    tryRequest(routingId: BufferLike, message: MessageLike,
               options?: { timeout?: number }): Promise<Received>;
    tryRequest(routingId: BufferLike, parts: readonly MessageLike[],
               options?: { timeout?: number }): Promise<Received>;
    tryRequest(routingId: BufferLike, message: MessageLike,
               callback: (err: Error | null, reply?: Received) => void,
               options?: { timeout?: number }): void;
    tryRequest(routingId: BufferLike, parts: readonly MessageLike[],
               callback: (err: Error | null, reply?: Received) => void,
               options?: { timeout?: number }): void;

    reply(routingId: BufferLike, requestSeq: bigint, message: MessageLike): number;
    reply(routingId: BufferLike, requestSeq: bigint, parts: readonly MessageLike[]): number;
    tryReply(routingId: BufferLike, requestSeq: bigint, message: MessageLike): SendResult;
    tryReply(routingId: BufferLike, requestSeq: bigint, parts: readonly MessageLike[]): SendResult;

    recv(): Received;
    tryRecv(): Received | null;
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
    tryRecv(): SocketMonitorEventValue | null;
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
    tryRecv(): ServiceEvent | null;
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
    publish(topic: string, payload: MessageLike): void;
    publish(topic: string, payloadParts: readonly MessageLike[]): void;
    tryPublish(topic: string, payload: MessageLike): SendResult;
    tryPublish(topic: string, payloadParts: readonly MessageLike[]): SendResult;
    setSubscription(topicOrPattern: string): void;
    unsetSubscription(topicOrPattern: string): void;
    subscribe(): Subscribed;
    trySubscribe(): Subscribed | null;
    onSubscribe(handler: SpotSubHandler): void;
    onSendReady(handler: SpotSendReadyHandler): void;
    setLinger(milliseconds: number): void;
    setSendHighWaterMark(value: number): void;
    setReceiveHighWaterMark(value: number): void;
    setSendTimeout(milliseconds: number): void;
    setReceiveTimeout(milliseconds: number): void;
    setNoDrop(enabled: boolean): void;

    // --- routed send (spot → spot) ---
    sendSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
             message: MessageLike): void;
    sendSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
             parts: readonly MessageLike[]): void;
    trySendSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                message: MessageLike): SendResult;
    trySendSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                parts: readonly MessageLike[]): SendResult;

    // --- routed request (spot → spot) ---
    requestSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                message: MessageLike,
                options?: { timeout?: number }): Promise<Received>;
    requestSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                parts: readonly MessageLike[],
                options?: { timeout?: number }): Promise<Received>;
    requestSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                message: MessageLike,
                callback: (err: Error | null, reply?: Received) => void,
                options?: { timeout?: number }): void;

    // --- routed reply (spot → spot) ---
    replySpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
              requestSeq: bigint, message: MessageLike): void;
    replySpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
              requestSeq: bigint, parts: readonly MessageLike[]): void;

    // --- routed send (spot → router) ---
    sendRouter(peerRid: BufferLike, message: MessageLike): void;
    sendRouter(peerRid: BufferLike, parts: readonly MessageLike[]): void;
    trySendRouter(peerRid: BufferLike, message: MessageLike): SendResult;
    trySendRouter(peerRid: BufferLike, parts: readonly MessageLike[]): SendResult;

    // --- routed request (spot → router) ---
    requestRouter(peerRid: BufferLike, message: MessageLike,
                  options?: { timeout?: number }): Promise<Received>;
    requestRouter(peerRid: BufferLike, parts: readonly MessageLike[],
                  options?: { timeout?: number }): Promise<Received>;
    requestRouter(peerRid: BufferLike, message: MessageLike,
                  callback: (err: Error | null, reply?: Received) => void,
                  options?: { timeout?: number }): void;

    // --- routed reply (spot → router) ---
    replyRouter(peerRid: BufferLike, requestSeq: bigint,
                message: MessageLike): void;
    replyRouter(peerRid: BufferLike, requestSeq: bigint,
                parts: readonly MessageLike[]): void;

    // --- routed receive ---
    recvRouted(): Received;
    tryRecvRouted(): Received | null;
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
