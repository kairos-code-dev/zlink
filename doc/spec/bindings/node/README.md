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

---

## Current Core Alignment Overrides

The sections below still contain some older signatures. When they conflict
with the rules here, this section wins.

- `PairSocket`, `DealerSocket`, and `RouterSocket` are recv-only on the data
  plane. Remove `onReceive(...)` from their public contract.
- `SubSocket` and `XSubSocket` are recv-only. Remove `onSubscribe(...)` from
  their public contract.
- `StreamSocket` keeps `recv(...)` and exposes a packet callback surface
  mapped to `zlink_stream_packet_handler()`. Recommended canonical name:
  `onPacket(...)`.
- `SpotNode` must expose channel-aware attachment APIs:
  `attachDiscovery(discovery)`,
  `attachChannelDealer(discovery, dealer)`,
  `attachChannelDealerManual(channelName, dealer)`, and
  `attachPubIngress(pub)`.
- `Spot` must expose channel-aware data-plane methods:
  `sendChannel(...)`, `requestChannel(...)`, and
  `publish(serviceName, topic, ...)`.
- `Spot.subscribe(...)` returns a service-aware `TopicMessage`.
  `TopicMessage` therefore needs `serviceName: string | null`, populated for
  SPOT subscribe results and `null` for raw `SUB` / `XSUB`.
- `Spot` must not expose `onSubscribe(...)`.
- `Spot` direct RID send/request APIs are removed from the public contract.
- Every socket and `Spot` exposes `setAdmissionState(state)` /
  `getAdmissionState()` using the typed enum-like object
  `AdmissionState.Serving` (1) and `AdmissionState.Draining` (2). Submit
  attempts to a drained peer throw `SubmitError` whose `code` equals
  `SubmitResult.NotAdmitted`.
- `POLLOUT` is a send-recovery readiness signal, shared with
  `onSendReady(...)`. It is not a "transport writable" bit.
- ROUTER / PUB socket option defaults follow the core header: `mandatory =
  true`, `handover = true`, `nodrop = true`.
- Internal pairing rule: when auto-connect pairs two same-service ROUTERs
  via Discovery, the library picks one initiator per pair by a total order
  on `(routingId, advertiseEndpoint)`. Users do not configure this.

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

All sockets expose `bind()`, `unbind()`, and `close()` from `BaseSocket`.
Connectable sockets also expose `connect()` and `disconnect()`.

Node / TypeScript nonblocking data-plane helpers follow this rule:

- `send(...)` and `publish(...)` return `false` only for temporary
  backpressure when `SendFlags.DontWait` is used.
- Blocking submit returns `true` on success. Route-not-ready and other submit
  failures still throw `SubmitError`.
- `recv(...)` and `subscribe(...)` return `null` when
  `RecvFlags.DontWait` finds no message and still throw `RecvError` for real
  recv failures.

All sockets (and `Spot`) also expose the admission-state accessor pair:

```typescript
interface AdmissionGates {
    /** @throws {ConfigError} */
    getAdmissionState(): AdmissionState;
    /** @throws {ConfigError} */
    setAdmissionState(state: AdmissionState): void;
}
```

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
    send(message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    send(parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    recv(flags?: RecvFlags): Received | null;
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
    publish(topic: string, message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): boolean;
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
    subscribe(flags?: RecvFlags): TopicMessage | null;
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
    setRoutingId(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    getRoutingId(): RoutingId;
    /** @throws {SubmitError} */
    send(message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    send(parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    recv(flags?: RecvFlags): Received | null;
    /** @throws {HandlerError} */
    onSendReady(handler: SocketSendReadyHandler): void;
    /** @throws {ConfigError} */
    attachDiscovery(discovery: Discovery): void;

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
            callback: RequestResultCallback,
            flags?: SendFlags,
            timeout?: number): boolean;
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(parts: readonly MessageLike[],
            callback: RequestResultCallback,
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
    /** @throws {ConnectError} */
    unbind(endpoint: string): void;
    /** @throws {ConnectError} */
    connect(endpoint: string): void;
    /** @throws {ConnectError} */
    disconnect(endpoint: string): void;
    /** @throws {ConfigError} */
    setRoutingId(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    getRoutingId(): RoutingId;
    /** @throws {SubmitError} */
    send(routingId: RoutingId, message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    send(routingId: RoutingId, parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
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
            callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    request(peerRid: RoutingId, parts: readonly MessageLike[],
            callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;

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
                  callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly (not a `RequestError`).
     */
    requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                  parts: readonly MessageLike[],
                  callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;

    // --- router → spot routed reply ---
    /** @throws {SubmitError} */
    replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
    /** @throws {SubmitError} */
    replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId,
                requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;

    // NOTE: RouterSocket 의 routed 수신 plane 은 단일 recv 표면이다. 일반
    // ROUTER 트래픽과 spot-origin routed 트래픽을 모두 recv 로 받는다.
    // `Received.routingId` 는 source_node_rid, `Received.spotRid` 는
    // spot-origin 트래픽에서만 값이 있다. data-plane callback install
    // surface (예: onReceive) 는 ROUTER 에 제공하지 않는다. request
    // completion 은 request() 경로에서만 유지된다.

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
    publish(topic: string, message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): boolean;
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
    subscribe(flags?: RecvFlags): TopicMessage | null;
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
    setRoutingId(routingId: RoutingId): void;
    /** @throws {ConfigError} */
    getRoutingId(): RoutingId;
    /** @throws {SubmitError} */
    send(routingId: RoutingId, message: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    send(routingId: RoutingId, parts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    /**
     * Three mutually-exclusive receive modes on the same StreamSocket:
     *   (1) recv(), (2) onPacket(handler). Second attach throws
     *   HandlerError(HandlerResult.Busy).
     * @throws {RecvError}
     */
    recv(flags?: RecvFlags): Received | null;
    /**
     * Mode (3): framed packet callback mapped to
     * `zlink_stream_packet_handler()`. Wire frame is big-endian u16
     * header_size + u32 body_size + header + body. Handler receives the
     * source routing id, a header Message, and a body Message; both
     * messages transfer ownership to the handler.
     * @throws {HandlerError}
     */
    onPacket(handler: StreamPacketHandler): void;
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
    connectRoutingId: RoutingId; // set only
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

The binding exposes separate codec extension packages. The public package names
are fixed to:

- `@ulalax/zlink-codec-protobuf`
- `@ulalax/zlink-codec-json`
- `@ulalax/zlink-codec-messagepack`

These are separate public packages layered on top of the core package. They
must not be merged into the root package entrypoint.

JSON codec baseline: built-in `JSON.parse` / `JSON.stringify`. Typed validation
may be layered on top through a schema/parser object.
MessagePack codec baseline: `@msgpack/msgpack`.

```typescript
declare module "@ulalax/zlink-codec-protobuf" {
    export interface ProtoDecoder<T> {
        decode(data: Uint8Array): T;
    }

    export interface ProtoEncoder<T> {
        encode(value: T): Uint8Array;
    }

    export function parseProto<T>(
        message: import("@ulalax/zlink").Message,
        decoder: ProtoDecoder<T>,
    ): T;

    export function toMessage<T>(
        value: T,
        encoder: ProtoEncoder<T>,
    ): import("@ulalax/zlink").Message;
}
```

```typescript
declare module "@ulalax/zlink-codec-json" {
    export interface JsonSchema<T> {
        parse(value: unknown): T;
    }

    export function parseJson(
        message: import("@ulalax/zlink").Message,
    ): unknown;

    export function parseJson<T>(
        message: import("@ulalax/zlink").Message,
        schema: JsonSchema<T>,
    ): T;

    export function toMessage(value: unknown): import("@ulalax/zlink").Message;
}
```

```typescript
declare module "@ulalax/zlink-codec-messagepack" {
    export interface MessagePackDecoder<T> {
        decode(data: Uint8Array): T;
    }

    export interface MessagePackEncoder<T> {
        encode(value: T): Uint8Array;
    }

    export function parseMessagePack<T>(
        message: import("@ulalax/zlink").Message,
        decoder: MessagePackDecoder<T>,
    ): T;

    export function toMessage<T>(
        value: T,
        encoder: MessagePackEncoder<T>,
    ): import("@ulalax/zlink").Message;
}
```

### RoutingId

Routing-id value object. Binary-safe, 1-255 bytes, immutable. The binding
exposes this wrapper type (not a raw `Buffer`) everywhere a routing id
appears in the public surface.

```typescript
class RoutingId {
    /** @throws {ConfigError} */
    static fromBytes(bytes: Buffer | Uint8Array): RoutingId;
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
  Use `fromBytes` for construction and `toHex()` / `toString()` for
  display.
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
    readonly spotRid: RoutingId | null;      // SPOT routed recv 에서만 값 있음
    readonly requestSeq: bigint | null;      // set on request-reply recv paths, else null
    readonly parts: Message[];

    isSinglePart(): boolean;
    /** @throws {RecvError} */
    firstPart(): Message;
    /** @throws {RecvError} */
    singlePartOrThrow(): Message;

    /** reply — requestSeq 가 null 이 아니어야 함. null 또는 invalid reply context 는 SubmitError. */
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
    NotAdmitted: 13,     // target peer is in AdmissionState.Draining
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
    recv(): MonitorEvent;
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
    readonly event: MonitorEventType;        // CONNECTION_READY, CONNECTED, DISCONNECTED, PEER_ADMISSION_CHANGED, ...
    readonly value: number;                  // uint32 — event-specific payload (PEER_ADMISSION_CHANGED carries the new AdmissionState)
    readonly routingId: RoutingId | null;    // peer routing id (null when not applicable)
    readonly localAddr: string;              // local endpoint
    readonly remoteAddr: string;             // remote endpoint
}
```

`MonitorEventType` includes `peerAdmissionChanged` (bit 15). Service
monitors surface the same change through
`ServiceMonitorEventMask.peerAdmissionChanged` (bit 8).

### ServiceMonitor

Starts in recv model. `onEvent(...)` transitions one-way to callback-only
model; after that `recv()` throws a busy recv error and `snapshot()` still works.

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

Runtime snapshot returned by `MonitorSocket.snapshot()` and
`ServiceMonitor.snapshot()`.

```typescript
interface MonitorSnapshot {
    readonly sourceKind: number;             // monitor source kind enum
    readonly stateFlags: number;             // uint32 bitmask
    readonly detailFlags: number;            // uint32 bitmask
    readonly sndPendingMsgs: bigint;         // uint64 send-queue depth
    readonly rcvPendingMsgs: bigint;         // uint64 recv-queue depth
    /** Convenience helper — returns true when `stateFlags` has the ready bit set. */
    isReady(): boolean;
}
```

### ServiceEvent

```typescript
class ServiceEvent {
    readonly serviceKind: number;            // zlink_service_type_t (SPOT, SOCKET, ...)
    readonly eventType: number;              // UP, DOWN, PROVIDERS_CHANGED, ERROR, ...
    readonly status: number;                 // uint32 status code
    readonly errorCode: number;              // uint32 errno (0 when not an error)
    readonly value: bigint;                  // uint64 event-specific value
    readonly detailFlags: number;            // uint32 detail bitmask
    readonly serviceName: string;
    readonly endpoint: string;
    readonly routingId: RoutingId | null;    // peer routing id (null when not applicable)
    readonly subject: string;                // subscribe subject (topic)
    readonly subjectKind: number;            // subject kind enum
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
export enum DiscoveryDealerPeerMode {
    Router = 1,
    Dealer = 2,
}

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
    /**
     * Resolve the current owner node routing id for a logical spot routing id.
     * Maps to zlink_discovery_resolve_spot.
     */
    resolveSpot(spotRid: RoutingId): RoutingId;
    /**
     * Set the auto-connect target policy used by DEALER sockets in this
     * discovery view. Default is Router. Maps to zlink_discovery_set_dealer_peer_mode.
     */
    setDealerPeerMode(mode: DiscoveryDealerPeerMode): void;
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
    attachChannelDealer(discovery: Discovery, dealer: DealerSocket): void;
    /** @throws {ConfigError} */
    attachChannelDealerManual(channelName: string, dealer: DealerSocket): void;
    /** @throws {ConfigError} */
    attachPubIngress(pub: PubSocket): void;
    /** @throws {ConfigError} */
    setTlsServer(cert: string, key: string, requireClient?: number): void;
    /** @throws {ConfigError} */
    setTlsClient(ca: string, host: string, trust?: number): void;
    /** @throws {ConfigError} */
    createSpot(): Spot;
    /** @throws {ConfigError} */
    statusSnapshot(): SpotNodeStatus;
    /** @throws {ConfigError} */
    peersSnapshot(): SpotNodePeerEntry[];
    /** @throws {ConfigError} */
    peersQuery(filter?: SpotNodePeerFilter): SpotNodePeerEntry[];
    /** @throws {ConfigError} */
    subjectsSnapshot(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[];
    /** @throws {ConfigError} */
    // --- identity / routing ---
    /**
     * SpotNode's logical address. Maps to zlink_set_routing_id(node, ...) /
     * zlink_get_routing_id(node, ...).
     */
    setRoutingId(rid: RoutingId): void;
    readonly routingId: RoutingId;

    // close() cascades: closes all live Spot handles before the node becomes invalid.
    /** @throws {CloseError} */
    close(): void;
}
```

`SpotNode` owns the lifecycle. `Spot` is created only through
`SpotNode.createSpot()`. Direct `new Spot(node)` construction is internal
and is not part of the public API contract.

### Spot

```typescript
class Spot {
    // The SpotNode constructor path is internal. Public code must use SpotNode.createSpot().
    /** @throws {SubmitError} */
    publish(serviceName: string, topic: string, payload: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    publish(serviceName: string, topic: string, payloadParts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    sendChannel(channelName: string, payload: MessageLike, flags?: SendFlags): boolean;
    /** @throws {SubmitError} */
    sendChannel(channelName: string, payloadParts: readonly MessageLike[], flags?: SendFlags): boolean;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestChannel(channelName: string, message: MessageLike, timeout?: number): Promise<Message[]>;
    /** @throws {ZlinkError} Rejects with `SubmitError` on submit failure or `RequestError` on reply failure. */
    requestChannel(channelName: string, parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly.
     */
    requestChannel(channelName: string, message: MessageLike,
                   callback: RequestResultCallback,
                   flags?: SendFlags,
                   timeout?: number): boolean;
    /**
     * @throws {SubmitError} on submit failure other than temporary backpressure.
     * Callback receives `RequestResult` directly.
     */
    requestChannel(channelName: string, parts: readonly MessageLike[],
                   callback: RequestResultCallback,
                   flags?: SendFlags,
                   timeout?: number): boolean;
    /** @throws {ConfigError} */
    setSubscription(topicOrPattern: string): void;
    /** @throws {ConfigError} */
    unsetSubscription(topicOrPattern: string): void;
    /** @throws {RecvError} */
    subscribe(flags?: RecvFlags): TopicMessage | null;
    /** @throws {RecvError} */
    receiveSubscriptionEvent(flags?: RecvFlags): SubscriptionEvent;
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

    // --- routed receive ---
    /** @throws {RecvError} */
    recvRouted(flags?: RecvFlags): Received;
    /** @throws {HandlerError} */
    onRoutedReceive(handler: SpotRoutedHandler): void;
    /** @throws {HandlerError} */
    onDispatchEvent(handler: SpotDispatchEventHandler): void;

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

### Service-Layer Entry Types

Value objects returned by `Registry`, `Discovery`, `SpotNode`, and
`RegistryQueryClient` snapshot / query calls. All fields are `readonly`
and every routing-id field is a `RoutingId` wrapper (never a raw
`Buffer`).

Primary entry types used in the default service flow:

```typescript
/** Discovery / Registry member peer entry. */
interface MemberPeerEntry {
    readonly serviceType: number;            // zlink_service_type_t
    readonly serviceRole: number;            // uint16 service role
    readonly serviceName: string;
    readonly endpoint: string;
    readonly routingId: RoutingId;
    readonly value: bigint;                  // int64 user value
    readonly admissionState: AdmissionState; // Serving | Draining
}

/** Registry topology entry (full topology view). */
interface RegistryTopologyEntry {
    readonly routingId: RoutingId;
    readonly serviceKind: number;            // zlink_service_kind_t
    readonly serviceRole: number;            // zlink_service_role_t
    readonly serviceName: string;
    readonly endpoint: string;
    readonly source: number;                 // zlink_topology_source_t
    readonly state: number;                  // zlink_topology_state_t
    readonly desiredCount: number;           // uint32
    readonly readyCount: number;             // uint32
    readonly errorCode: number;              // uint32 errno
    readonly lastReportedMs: bigint;         // uint64 epoch ms
}

/** SpotNode runtime status snapshot. */
interface SpotNodeStatus {
    readonly serviceName: string;
    readonly localEndpoint: string;
    readonly nodeRoutingId: RoutingId;
    readonly state: number;                  // zlink_spot_node_state_t
    readonly configuredPeerCount: number;    // uint32
    readonly activePeerCount: number;        // uint32
    readonly connectedPeerCount: number;     // uint32
    readonly subjectCount: number;           // uint32
    readonly readySubjectCount: number;      // uint32
    readonly lastError: number;              // int32
    readonly lastChangedMs: bigint;          // uint64 epoch ms
}
```

Advanced / Diagnostic entry types and filters:

```typescript
/** Registry service summary roll-up entry. */
interface RegistryServiceSummaryEntry {
    readonly serviceKind: number;            // zlink_service_kind_t
    readonly serviceRole: number;            // zlink_service_role_t
    readonly serviceName: string;
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
    readonly state: number;                  // zlink_registry_state_t
    readonly topologyEntryCount: number;     // uint32
    readonly peerRegistryCount: number;      // uint32
    readonly connectedPeerRegistryCount: number; // uint32
    readonly listSeq: bigint;                // uint64
    readonly lastError: number;              // int32
    readonly lastChangedMs: bigint;          // uint64 epoch ms
}

/** SpotNode peer entry. */
interface SpotNodePeerEntry {
    readonly serviceName: string;
    readonly localEndpoint: string;
    readonly peerEndpoint: string;
    readonly source: number;                 // zlink_spot_peer_source_t
    readonly state: number;                  // zlink_spot_peer_state_t
    readonly admissionState: AdmissionState; // Serving | Draining
    readonly connectedSinceMs: bigint;       // uint64 epoch ms
    readonly lastChangedMs: bigint;          // uint64 epoch ms
}

/** SpotNode subject entry. */
interface SpotNodeSubjectEntry {
    readonly role: number;                   // zlink_spot_role_t
    readonly subject: string;
    readonly subjectKind: number;            // uint32
    readonly readyPeerCount: number;         // uint32
    readonly activePeerCount: number;        // uint32
    readonly lastChangedMs: bigint;          // uint64 epoch ms
}

/** Admission state for a peer. */
const AdmissionState = {
    Serving: 1,
    Draining: 2,
} as const;

type AdmissionState = typeof AdmissionState[keyof typeof AdmissionState];

/** Filter for Registry service summary snapshot. */
interface RegistryServiceSummaryFilter {
    readonly serviceKind?: number;
    readonly serviceRole?: number;
    readonly serviceName?: string;
}

/** Filter for Registry topology snapshot / query. */
interface RegistryTopologyFilter {
    readonly serviceKind?: number;
    readonly serviceRole?: number;
    readonly serviceName?: string;
    readonly routingId?: RoutingId;
    readonly state?: number;
    readonly source?: number;
}

/** Filter for SpotNode peers query. */
interface SpotNodePeerFilter {
    readonly peerEndpoint?: string;
    readonly source?: number;
    readonly state?: number;
}

/** Filter for SpotNode subjects query. */
interface SpotNodeSubjectFilter {
    readonly role?: number;
    readonly subject?: string;
    readonly subjectKind?: number;
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
    /** Number of registered pollable items. Maps to zlink_poller_size. */
    readonly size: number;
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
type SocketRecvHandler = (message: Received) => void;
type SocketSubscribeHandler = (message: TopicMessage) => void;
type SocketSendReadyHandler = () => void;
type StreamPacketHandler = (sourceRid: RoutingId,
                            header: Message,
                            body: Message) => void;
type SpotSubHandler = SocketSubscribeHandler;
type SpotSendReadyHandler = () => void;
type SpotRoutedHandler = (sourceRid: RoutingId | null, spotRid: RoutingId | null,
                          requestSeq: bigint, parts: Message[]) => void;
type SpotDispatchEventHandler = (event: number) => void;
type RequestResultCallback = (result: RequestResult, parts: Message[]) => void;
type TimerHandler = (timer: Timer, fireCount: bigint) => void;
type ServiceMonitorHandler = (event: ServiceEvent) => void;
type ServiceMonitorEventMask = number;
/** Monitor event type enum (CONNECTION_READY, CONNECTED, DISCONNECTED, ...). */
type MonitorEventType = number;
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
