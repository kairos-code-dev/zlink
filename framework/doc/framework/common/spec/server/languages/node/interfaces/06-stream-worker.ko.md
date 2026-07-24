# Node.js STREAM, timer와 worker 공개 인터페이스

[인터페이스 목차](README.ko.md) · [Node.js 계약 목차](../README.ko.md)

이 문서는 ZLink Framework 11.0.0에서 `@zlink-systems/framework`와
`@zlink-systems/nestjs`가 내보내는 STREAM, timer와 worker 관련 정확한 TypeScript declaration을 고정한다.
동작 의미는 [공통 스펙](../../../../README.ko.md)이 소유하며, 이 문서는 이름, generic, overload,
상속, member, parameter와 반환형만 정의한다.

## 1. Spot handler와 STREAM

```ts
export declare enum ZLinkSpotPeerState {
    Configured = 1,
    Connecting = 2,
    Connected = 3
}

export interface ZLinkSpotPublisherClient {
    publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}

export declare function ZLinkSpotRequest(packetName?: string): MethodDecorator;

export interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
    handle(spot: TSpot, request: TRequest, context: ZLinkHandlerContext): Promise<TReply>;
}

export declare function ZLinkSpotSubscription(channelName: string, topic: string): MethodDecorator;

export interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
    handle(spot: TSpot, event: TEvent, context: ZLinkPublishContext): Promise<void>;
}

export interface ZLinkSpotTimerDiagnostic {
    readonly spotId: SpotId;
    readonly isEntrySpot: boolean;
    readonly timerName: string;
    readonly handlerType: string;
    readonly deliveryIndex: bigint;
    readonly scheduledIndex: bigint;
    readonly exceptionType: string;
    readonly exceptionMessage: string;
}

export interface ZLinkSpotTimerHandler<TSpot> {
    handle(spot: TSpot, tick: ZLinkTimerTick): Promise<void>;
}

export interface ZLinkStream {
    readonly sessionId: string;
    readonly routingId?: RoutingId;
    readonly localAddr?: string;
    readonly remoteAddr?: string;
    write(payload: ZLinkMessage, flags?: number): boolean;
    close(signal?: AbortSignal): Promise<void>;
}
```

## 2. STREAM node, compression과 timer

```ts
export interface ZLinkStreamCompressionBuilder {
    useDefault(): this;
    useLz4(): this;
    use(codec: ZLinkStreamCompressionCodec): this;
    disable(): this;
}

export interface ZLinkStreamCompressionCodec {
    compress(payload: Uint8Array): Uint8Array;
    decompress(payload: Uint8Array, maxDecompressedSize: number): Uint8Array;
}

export interface ZLinkStreamCompressionOptions {
    readonly disabled?: boolean;
    readonly codec?: ZLinkStreamCompressionCodec;
}

export interface ZLinkStreamDiagnostic {
    readonly nativeCode?: number;
    readonly message?: string | undefined;
}

export interface ZLinkStreamError {
    readonly error: ZLinkStreamSessionError;
    readonly diagnostic?: ZLinkStreamDiagnostic;
}

export interface ZLinkStreamNodeBuilder {
    bind(endpoint: string): this;
    bind(port?: number): this;
    setBindHost(bindHost: string): this;
    setAdvertiseHost(advertiseHost: string): this;
    enableActorDispatch(): this;
    setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
    registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}

export declare function ZLinkStreamPacket(): MethodDecorator;

export declare function ZLinkStreamRaw(): MethodDecorator;

export declare enum ZLinkStreamSessionError {
    Internal = "internal",
    TransportError = "transportError"
}

export interface ZLinkTimer {
    readonly isDisposed: boolean;
    cancel(signal?: AbortSignal): Promise<void>;
    dispose(): Promise<void>;
}

export interface ZLinkTimerOptions {
    overrunPolicy?: ZLinkTimerOverrunPolicy;
    maxCatchUpTicks?: number;
    stopOnUnhandledException?: boolean;
}
```

## 3. Timer scheduling과 worker

```ts
export declare enum ZLinkTimerOverrunPolicy {
    SkipLateTicks = "skipLateTicks",
    CatchUpBounded = "catchUpBounded",
    DelayNextTick = "delayNextTick"
}

export interface ZLinkTimerTick {
    readonly name: string;
    readonly deliveryIndex: bigint;
    readonly scheduledIndex: bigint;
    readonly periodMs: number;
    readonly scheduledAt: Date;
    readonly startedAt: Date;
    readonly scheduledElapsedMs: number;
    readonly startedElapsedMs: number;
    readonly delayMs: number;
    readonly skippedTicks: bigint;
}

export declare enum ZLinkUnhandledDispatchAction {
    ReplyError = "replyError",
    LogAndDrop = "logAndDrop",
    Drop = "drop",
    Throw = "throw"
}

export interface ZLinkUnhandledDispatchOptions {
    request: ZLinkUnhandledDispatchAction;
    send: ZLinkUnhandledDispatchAction;
    publish: ZLinkUnhandledDispatchAction;
}

export interface ZLinkWorkerCall<T> {
    timeoutMs(durationMs: number): ZLinkWorkerCall<T>;
    submit(signal?: AbortSignal): void;
    async(signal?: AbortSignal): Promise<T>;
    yield(signal?: AbortSignal): Promise<T>;
}

export interface ZLinkWorkerOptions {
    readonly minThreads: number;
    readonly maxThreads: number;
    readonly idleTimeoutMs: number;
    readonly maxQueueLength: number;
}
```

Request와 join의 result-bearing `submit()`은 공통 `Async` 의미이며 terminal reply 또는 결과까지 현재
owner turn을 유지한다. Worker call은 결과를 기다리지 않는 `submit()`과 결과까지 현재 turn을 유지하는
`async()`를 제공한다. `yield()`는 `SpotWide` User Spot 또는 Instance Spot의 shared turn에서만 그 turn을
반환한다. 다른 실행 문맥에서는 worker를 제출하거나 turn을 반환하지 않고 `invalidConfiguration`으로
완료한다.
