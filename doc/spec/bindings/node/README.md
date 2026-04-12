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
    /** @throws {CloseError} */
    shutdown(): void;
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
    blocky: boolean;            // get / set — @throws {ConfigError}
    /** @throws {ConfigError} */
    addThreadAffinity(cpu: number): void;
    /** @throws {ConfigError} */
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

All sockets expose `bind()`, `unbind()`, and `close()` from `BaseSocket`.
Connectable sockets also expose `connect()` and `disconnect()`.

### PairSocket

```typescript
class PairSocket {
    constructor(ctx: Context);
    readonly options: CommonSocketOptions;
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {SubmitError} */
    send(message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    send(parts: readonly MessageLike[], flags?: SendFlags): void;
    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onReceive(handler: SocketRecvHandler): void;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
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
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {SubmitError} */
    publish(topic: string, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): void;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;
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
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConfigError} */
    setSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    unsetSubscription(topicOrPattern: string): void;
    /** @throws {RecvError} */
    subscribe(flags?: RecvFlags): Subscribed;
    /** @throws {HandlerError} */
    onSubscribe(handler: SocketSubscribeHandler): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;
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
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConfigError} */
    setRoutingId(routingId: BufferLike): void;
    /** @throws {ConfigError} */
    getRoutingId(): Buffer;
    /** @throws {SubmitError} */
    send(message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    send(parts: readonly MessageLike[], flags?: SendFlags): void;
    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onReceive(handler: SocketRecvHandler): void;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;
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
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConfigError} */
    setRoutingId(routingId: BufferLike): void;
    /** @throws {ConfigError} */
    getRoutingId(): Buffer;
    /** @throws {SubmitError} */
    send(routingId: BufferLike, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    send(routingId: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;
    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onReceive(handler: SocketRecvHandler): void;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;

    // --- router → spot routed send ---
    /** @throws {SubmitError} */
    sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
               message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
               parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- router → spot routed request (async) — no flags ---
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  message: MessageLike, timeout?: number): Promise<Received>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  parts: readonly MessageLike[], timeout?: number): Promise<Received>;

    // --- router → spot routed request (callback) — throws on submit failure ---
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  message: MessageLike,
                  callback: RequestResultCallback,
                  flags?: SendFlags, timeout?: number): void;
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  parts: readonly MessageLike[],
                  callback: RequestResultCallback,
                  flags?: SendFlags, timeout?: number): void;

    // --- router → spot routed reply ---
    /** @throws {SubmitError} */
    replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- router spot receive ---
    /** @throws {RecvError} */
    recvSpot(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onSpotReceive(handler: RouterSpotHandler): void;

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
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {SubmitError} */
    publish(topic: string, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): void;
    /** @throws {RecvError} */
    receiveSubscriptionEvent(flags?: RecvFlags): SubscriptionEvent;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
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
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConfigError} */
    setSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    unsetSubscription(topicOrPattern: string): void;
    /** @throws {RecvError} */
    subscribe(flags?: RecvFlags): Subscribed;
    /** @throws {HandlerError} */
    onSubscribe(handler: SocketSubscribeHandler): void;
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
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConfigError} */
    setRoutingId(routingId: BufferLike): void;
    /** @throws {ConfigError} */
    getRoutingId(): Buffer;
    /** @throws {SubmitError} */
    send(routingId: BufferLike, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    send(routingId: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;
    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onReceive(handler: SocketRecvHandler): void;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
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
    /** @throws {ConfigError} */
    constructor(data: Buffer);
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

### RoutingId

Routing IDs are represented as `Buffer` values (1-255 bytes).

### Received

```typescript
class Received {
    readonly routingId: Buffer | null;
    readonly parts: Message[];
    readonly requestSeq: bigint | null;
    toBytesList(): Buffer[];
    /** @throws {ConfigError} */
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
    /** @throws {ConfigError} */
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

Common base class of all zlink errors. Every failing operation throws one
of the eight concrete subclasses below, each of which corresponds to a C
API function-category result enum (`SubmitError`, `RequestError`,
`RecvError`, `HandlerError`, `CloseError`, `BindError`, `ConnectError`,
`ConfigError`). Catch `ZlinkError` for the "catch-all" idiom, or a
specific subclass when finer-grained handling is required. Method
declarations indicate which specific subclass they throw via TSDoc
`@throws` comments.

The `code` field is a globally unique `int` that spans all result enum
ranges (0-703). The code alone identifies the error without needing to
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
to the callback (`RequestResultCallback`) rather than throwing this
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

Thrown by handler registration methods (`onReceive`, `onSubscribe`,
`onSendReady`, `onSpotReceive`, `onFire`, `onEvent`, etc.). Wraps a
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

## Request-Reply

### RequestDealer

```typescript
class RequestDealer {
    constructor(socket: DealerSocket);
    socket(): DealerSocket;

    // Promise (async) — no flags, timeout = 0 uses socket default
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    request(message: MessageLike, timeout?: number): Promise<Received>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    request(parts: readonly MessageLike[], timeout?: number): Promise<Received>;

    // Callback — throws on submit failure, timeout = 0 uses socket default
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(message: MessageLike,
            callback: RequestResultCallback,
            flags?: SendFlags, timeout?: number): void;
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(parts: readonly MessageLike[],
            callback: RequestResultCallback,
            flags?: SendFlags, timeout?: number): void;

    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onReceive(handler: (received: Received) => void): void;
    /** @throws {CloseError} */
    close(): void;
}
```

### RequestRouter

```typescript
class RequestRouter {
    constructor(socket: RouterSocket);
    socket(): RouterSocket;

    // Promise (async) — no flags, timeout = 0 uses socket default
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    request(routingId: BufferLike, message: MessageLike,
            timeout?: number): Promise<Received>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    request(routingId: BufferLike, parts: readonly MessageLike[],
            timeout?: number): Promise<Received>;

    // Callback — throws on submit failure, timeout = 0 uses socket default
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(routingId: BufferLike, message: MessageLike,
            callback: RequestResultCallback,
            flags?: SendFlags, timeout?: number): void;
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(routingId: BufferLike, parts: readonly MessageLike[],
            callback: RequestResultCallback,
            flags?: SendFlags, timeout?: number): void;

    /** @throws {SubmitError} */
    reply(routingId: BufferLike, requestSeq: bigint, message: MessageLike,
          flags?: SendFlags): void;
    /** @throws {SubmitError} */
    reply(routingId: BufferLike, requestSeq: bigint, parts: readonly MessageLike[],
          flags?: SendFlags): void;

    /** @throws {RecvError} */
    recv(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onReceive(handler: (received: Received) => void): void;
    /** @throws {CloseError} */
    close(): void;
}
```

---

## Monitoring

### MonitorSocket

```typescript
class MonitorSocket {
    /** @throws {RecvError} */
    recv(): SocketMonitorEventValue;
    /** @throws {HandlerError} */
    onEvent(handler: (event: SocketMonitorEventValue) => void): void;
    /** @throws {ConfigError} */
    snapshot(): MonitorSnapshot;
    /** @throws {CloseError} */
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
    /** @throws {RecvError} */
    recv(): ServiceEvent;
    /** @throws {HandlerError} */
    onEvent(handler: (event: ServiceEvent) => void): void;
    /** @throws {ConfigError} */
    snapshot(): MonitorSnapshot;
    /** @throws {CloseError} */
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
    /** @throws {BindError} */
    bind(pubEndpoint: string, routerEndpoint: string): void;
    /** @throws {ConfigError} */
    setId(id: number): void;
    /** @throws {ConnectError} */
    addPeer(pubEndpoint: string): void;
    /** @throws {ConfigError} */
    setHeartbeat(intervalMs: number, timeoutMs: number): void;
    /** @throws {ConfigError} */
    setBroadcastInterval(intervalMs: number): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClient?: number): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, host: string, trust?: number): void;
    /** @throws {ConfigError} */
    statusSnapshot(): RegistryStatus;
    /** @throws {ConfigError} */
    serviceSummarySnapshot(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[];
    /** @throws {ConfigError} */
    topologySnapshot(): RegistryTopologyEntry[];
    /** @throws {ConfigError} */
    topologyQuery(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
    /** @throws {ConfigError} */
    memberPeers(serviceType: number, serviceName?: string): MemberPeerEntry[];
    /** @throws {ConfigError} */
    memberPeerMetadata(serviceType: number, serviceName: string,
                       serviceRole: number, endpoint: string): Buffer;
    /** @throws {CloseError} */
    close(): void;
}
```

### Discovery

```typescript
class Discovery {
    constructor(ctx: Context, serviceType: number, serviceName: string);
    readonly serviceType: number;
    readonly serviceName: string;
    /** @throws {ConnectError} */
    connectRegistry(endpoint: string): void;
    /** @throws {ConfigError} */
    setValue(value: number): void;
    /** @throws {ConfigError} */
    getValue(): number;
    /** @throws {ConfigError} */
    setMetadata(metadata: BufferLike | string): void;
    /** @throws {ConfigError} */
    getMetadata(): Buffer;
    /** @throws {ConfigError} */
    memberPeers(): MemberPeerEntry[];
    /** @throws {ConfigError} */
    memberPeerMetadata(serviceRole: number, endpoint: string): Buffer;
    /** @throws {ConfigError} */
    monitorOpen(events?: ServiceMonitorEventMask): ServiceMonitor;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, host: string, trust?: number): void;
    /** @throws {CloseError} */
    close(): void;
}
```

### SpotNode

```typescript
class SpotNode {
    constructor(ctx: Context);
    /** @throws {BindError} */
    bind(endpoint: string): void;
    /** @throws {ConnectError} */
    connectPeer(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnectPeer(endpoint: string): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClient?: number): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, host: string, trust?: number): void;
    /** @throws {ConfigError} */
    statusSnapshot(): SpotNodeStatus;
    /** @throws {ConfigError} */
    peersSnapshot(): SpotNodePeerEntry[];
    /** @throws {ConfigError} */
    peersQuery(filter?: SpotNodePeerFilter): SpotNodePeerEntry[];
    /** @throws {ConfigError} */
    subjectsSnapshot(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[];
    /** @throws {CloseError} */
    close(): void;
}
```

### Spot

```typescript
class Spot {
    constructor(node: SpotNode);
    /** @throws {SubmitError} */
    publish(topic: string, payload: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    publish(topic: string, payloadParts: readonly MessageLike[], flags?: SendFlags): void;
    /** @throws {ConfigError} */
    setSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    unsetSubscription(topicOrPattern: string): void;
    /** @throws {RecvError} */
    subscribe(flags?: RecvFlags): Subscribed;
    /** @throws {HandlerError} */
    onSubscribe(handler: SpotSubHandler): void;
    /** @throws {HandlerError} */
    onSendReady(handler: SpotSendReadyHandler): void;
    /** @throws {ConfigError} */
    setLinger(milliseconds: number): void;
    /** @throws {ConfigError} */
    setSendHighWaterMark(value: number): void;
    /** @throws {ConfigError} */
    setReceiveHighWaterMark(value: number): void;
    /** @throws {ConfigError} */
    setSendTimeout(milliseconds: number): void;
    /** @throws {ConfigError} */
    setReceiveTimeout(milliseconds: number): void;
    /** @throws {ConfigError} */
    setNoDrop(enabled: boolean): void;

    // --- routed send (spot → spot) ---
    /** @throws {SubmitError} */
    sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
               message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
               parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- routed request (spot → spot, async) — no flags ---
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  message: MessageLike, timeout?: number): Promise<Received>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  parts: readonly MessageLike[], timeout?: number): Promise<Received>;

    // --- routed request (spot → spot, callback) — throws on submit failure ---
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  message: MessageLike,
                  callback: RequestResultCallback,
                  flags?: SendFlags, timeout?: number): void;
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                  parts: readonly MessageLike[],
                  callback: RequestResultCallback,
                  flags?: SendFlags, timeout?: number): void;

    // --- routed reply (spot → spot) ---
    /** @throws {SubmitError} */
    replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike,
                requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- routed send (spot → router) ---
    /** @throws {SubmitError} */
    sendToRouter(peerRid: BufferLike, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    sendToRouter(peerRid: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- routed request (spot → router, async) — no flags ---
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestToRouter(peerRid: BufferLike, message: MessageLike,
                    timeout?: number): Promise<Received>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestToRouter(peerRid: BufferLike, parts: readonly MessageLike[],
                    timeout?: number): Promise<Received>;

    // --- routed request (spot → router, callback) — throws on submit failure ---
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToRouter(peerRid: BufferLike, message: MessageLike,
                    callback: RequestResultCallback,
                    flags?: SendFlags, timeout?: number): void;
    /**
     * @throws {SubmitError} on submit failure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToRouter(peerRid: BufferLike, parts: readonly MessageLike[],
                    callback: RequestResultCallback,
                    flags?: SendFlags, timeout?: number): void;

    // --- routed reply (spot → router) ---
    /** @throws {SubmitError} */
    replyToRouter(peerRid: BufferLike, requestSeq: bigint,
                  message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    replyToRouter(peerRid: BufferLike, requestSeq: bigint,
                  parts: readonly MessageLike[], flags?: SendFlags): void;

    // --- routed receive ---
    /** @throws {RecvError} */
    recvRouted(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onRoutedReceive(handler: SpotRoutedHandler): void;
    /** @throws {HandlerError} */
    onDispatchEvent(handler: SpotDispatchEventHandler): void;

    /** @throws {CloseError} */
    close(): void;
}
```

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

---

## Poller

```typescript
class Poller {
    constructor();

    // --- socket registration ---
    /** @throws {ConfigError} */
    addSocket(socket: BaseSocket, events: number, userData?: any): void;
    /** @throws {ConfigError} */
    modifySocket(socket: BaseSocket, events: number): void;
    /** @throws {ConfigError} */
    removeSocket(socket: BaseSocket): void;

    // --- file descriptor registration ---
    /** @throws {ConfigError} */
    addFd(fd: number, events: number, userData?: any): void;
    /** @throws {ConfigError} */
    modifyFd(fd: number, events: number): void;
    /** @throws {ConfigError} */
    removeFd(fd: number): void;

    // --- timer registration ---
    /** @throws {ConfigError} */
    addTimer(timer: Timer, userData?: any): void;
    /** @throws {ConfigError} */
    removeTimer(timer: Timer): void;

    // --- poll ---
    /** @throws {ConfigError} */
    size(): number;
    /** @throws {RecvError} */
    wait(timeoutMs: number): PollerEvent | null;
    /** @throws {RecvError} */
    waitAll(events: number, timeoutMs: number): PollerEvent[];
    /** @throws {RecvError} */
    poll(timeoutMs: number): number[];

    /** @throws {CloseError} */
    destroy(): void;
    /** @throws {CloseError} */
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

    /** @throws {ConfigError} */
    static fromSpot(spot: Spot): Timer;

    /** @throws {ConfigError} */
    start(intervalNs: bigint, repeatCount: bigint): void;
    /** @throws {ConfigError} */
    stop(): void;
    /** @throws {RecvError} */
    recv(flags?: number): bigint;
    /** @throws {HandlerError} */
    onFire(handler: TimerHandler): void;
    /** @throws {CloseError} */
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

    /** @throws {CloseError} */
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
