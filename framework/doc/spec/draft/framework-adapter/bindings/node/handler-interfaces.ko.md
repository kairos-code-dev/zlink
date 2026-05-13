<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework For Node.js](README.ko.md) | [다음: ZLink Framework NestJS Channel Messaging](nestjs-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[Node.js 묶음](./README.ko.md) | [channel](./nestjs-channel-messaging.ko.md) | [SPOT](./nestjs-spot.ko.md) | [STREAM](./nestjs-stream.ko.md) | [Monitoring](./nestjs-monitoring.ko.md) | [Registry](./nestjs-registry.ko.md)

# Draft -- ZLink Framework Node.js Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `Node.js`에서 `ZLink Framework`가 노출할 interface와
> decorator를 한 곳에 모은 기준 문서다.

## 0. 공통 정책 반영

이 문서는 [Framework Adapter 정책](../../policy/README.ko.md)과
[doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
규칙을 그대로 따른다. 따라서 `Node.js` 문서에서는 아래를 기본으로 본다.

- 메서드는 `camelCase`, 클래스와 decorator는 `PascalCase`를 쓴다.
- 개념 이름은 공통 정책과 맞춘다. 예를 들어 `send`, `request`, `publish`,
  `sendTo`, `requestTo`, `sendChannel`, `requestChannel` 같은 action 이름을
  유지한다.
- send/publish는 기본 async submit으로 설명한다. backpressure는 별도 public
  no-wait 옵션이 아니라 framework 내부의 nonblocking send, pending queue,
  ready notification으로 처리한다.
- 수동 연결은 `channel + capability` 또는 `spot node + capability` 단위로
  설명한다.

## 1. 기본 타입

```ts
export interface ZLinkHandlerContext {
  channelName?: string;
  packetName?: string;
  contentType?: string;
  correlationId?: string;
  deadline?: Date;
}

export interface ZLinkStream {
  sessionId: string;
  routingId?: string;
  localAddr?: string;
  remoteAddr?: string;

  write(payload: Message, flags?: SendFlags): Promise<void>;
  writePacket(header: Message, body: Message, flags?: SendFlags): Promise<void>;
}

export enum ZLinkStreamSessionError {
  Internal = 'internal',
  TransportError = 'transportError',
  HandshakeFailed = 'handshakeFailed',
}

export interface ZLinkStreamError {
  error: ZLinkStreamSessionError;
  internalErrno: number;
  getErrorCode(): ErrorCode;
  getErrorMessage(): string;
}

export interface ZLinkPacketStreamSession {
  onConnected(stream: ZLinkStream): Promise<void>;
  onDisconnected(stream: ZLinkStream): Promise<void>;
  onError(stream: ZLinkStream, error: ZLinkStreamError): Promise<void>;
  onPacket(stream: ZLinkStream, header: Message, body: Message): Promise<void>;
}

export interface ZLinkRawStreamSession {
  onConnected(stream: ZLinkStream): Promise<void>;
  onDisconnected(stream: ZLinkStream): Promise<void>;
  onError(stream: ZLinkStream, error: ZLinkStreamError): Promise<void>;
  onRaw(stream: ZLinkStream, payload: Message): Promise<void>;
}

export interface ZLinkSendOptions {
  packetName?: string;
}

export interface ZLinkRequestOptions {
  packetName?: string;
  timeoutMs?: number;
}

export interface ManualRoutedPeerEntry {
  targetRid: string;
  endpoint: string;
}

export interface ClientCapabilityOptions {
  manualConnections?: readonly string[];
}

export interface SubscriberCapabilityOptions {
  manualConnections?: readonly string[];
}

export interface ChannelOptions {
  server?: {};
  client?: ClientCapabilityOptions;
  publisher?: {};
  subscriber?: SubscriberCapabilityOptions;
}

export interface SpotRouterCapabilityOptions {
  manualConnections?: readonly ManualRoutedPeerEntry[];
}

export interface SpotPubSubCapabilityOptions {
  manualConnections?: readonly string[];
}

export interface SpotChannelClientCapabilityOptions {
  manualConnections?: readonly string[];
}

export interface SpotPublisherClientCapabilityOptions {
  manualConnections?: readonly string[];
}

export interface SpotFactoryEntry {
  spotName: string;
  spotType: Function;
}

export interface SpotNodeOptions {
  bind?: string;
  router?: SpotRouterCapabilityOptions;
  pubSub?: SpotPubSubCapabilityOptions;
  channelClients?: Record<string, SpotChannelClientCapabilityOptions>;
  spotPublishers?: Record<string, SpotPublisherClientCapabilityOptions>;
  spotFactories?: readonly SpotFactoryEntry[];
}

export interface ZLinkModuleOptions {
  channels?: Record<string, ChannelOptions>;
  discovery?: {
    registries: readonly string[];
  };
  spotDiscovery?: Record<string, { registries: readonly string[] }>;
  spotNodes?: Record<string, SpotNodeOptions>;
}

export interface ChannelClientConnections {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
  listConnections(): readonly string[];
}

export interface ChannelSubscriberConnections {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
  listConnections(): readonly string[];
}

export interface ZLinkChannelConnectionManager {
  getClient(channelName: string): ChannelClientConnections;
  getSubscriber(channelName: string): ChannelSubscriberConnections;
}

export interface ZLinkMonitoringOptions {
  addSocketEvents(sourceName: string, events?: SocketEvent): void;
  addDiscoveryEvents(
    sourceName: string,
    ...events: ServiceMonitorEventMask[]
  ): void;
  addRegistryEvents(sourceName: string, intervalMs: number): void;
  addSpotEvents(sourceName: string, intervalMs: number): void;
}

export interface ZLinkRuntimeEvent {
  sourceName: string;
  timestamp: Date;
}

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export enum ZLinkSocketEventKind {
  Connected = 'connected',
  ConnectionReady = 'connectionReady',
  Disconnected = 'disconnected',
  HandshakeFailed = 'handshakeFailed',
  PeerAdmissionChanged = 'peerAdmissionChanged',
  Closed = 'closed',
  Internal = 'internal',
}

export interface ZLinkSocketEvent extends ZLinkRuntimeEvent {
  event: ZLinkSocketEventKind;
  value: number;
  routingId?: string;
  localAddr: string;
  remoteAddr: string;
}

export enum ZLinkDiscoveryEventKind {
  ServiceUp = 'serviceUp',
  ServiceDown = 'serviceDown',
  ProvidersChanged = 'providersChanged',
  PeerAdmissionChanged = 'peerAdmissionChanged',
  Error = 'error',
  Closed = 'closed',
  Internal = 'internal',
}

export interface ZLinkDiscoveryEvent extends ZLinkRuntimeEvent {
  event: ZLinkDiscoveryEventKind;
  status: number;
  errorCode: number;
  serviceName: string;
  endpoint: string;
  routingId?: string;
}

export enum ZLinkRegistryEventKind {
  StatusChanged = 'statusChanged',
  TopologyChanged = 'topologyChanged',
  ServiceSummaryChanged = 'serviceSummaryChanged',
}

export interface ZLinkRegistryEvent extends ZLinkRuntimeEvent {
  event: ZLinkRegistryEventKind;
}

export enum ZLinkSpotEventKind {
  StatusChanged = 'statusChanged',
  PeersChanged = 'peersChanged',
  SubjectsChanged = 'subjectsChanged',
}

export interface ZLinkSpotEvent extends ZLinkRuntimeEvent {
  event: ZLinkSpotEventKind;
}
```

현재 초안에서는 capability 값을 `boolean`과 object로 섞지 않고, 항상 object로 두는
편을 기준으로 본다. 즉 `client: {}`는 client capability만 켠다는 뜻이고,
`client: { manualConnections: [...] }`는 같은 capability의 manual 연결까지 같이
준다는 뜻이다.

## 2. Client

```ts
export interface ZLinkClient {
  send<TMessage>(
    channelName: string,
    message: TMessage,
    options?: ZLinkSendOptions
  ): Promise<boolean>;

  request<TReply>(
    channelName: string,
    request: unknown,
    options?: ZLinkRequestOptions
  ): Promise<TReply>;
}

export interface ZLinkSpotClient {
  sendChannel<TMessage>(
    channelName: string,
    message: TMessage,
    options?: ZLinkSendOptions
  ): Promise<boolean>;

  requestChannel<TReply>(
    channelName: string,
    request: unknown,
    options?: ZLinkRequestOptions
  ): Promise<TReply>;

  sendTo<TMessage>(
    targetRid: string,
    spotRid: string,
    message: TMessage,
    options?: ZLinkSendOptions
  ): Promise<boolean>;

  requestTo<TReply>(
    targetRid: string,
    spotRid: string,
    request: unknown,
    options?: ZLinkRequestOptions
  ): Promise<TReply>;

  publish<TEvent>(
    topic: string,
    message: TEvent,
    options?: ZLinkSendOptions
  ): Promise<boolean>;
}

export interface ZLinkSpotPublisherClient {
  publish<TEvent>(
    channelName: string,
    topic: string,
    message: TEvent,
    options?: ZLinkSendOptions
  ): Promise<boolean>;
}

export interface ZLinkEventPublisher {
  publish<TEvent>(
    channelName: string,
    topic: string,
    message: TEvent,
    options?: ZLinkSendOptions
  ): Promise<boolean>;
}

export interface ZLinkSpotCreateResult {
  spotRid: string;
  spotName: string;
  created: boolean;
}

export interface ZLinkSpotInfo {
  spotRid: string;
  spotName: string;
}

export interface ZLinkSpotManager {
  create(spotName: string): Promise<ZLinkSpotCreateResult>;
  create(
    spotName: string,
    spotRid: string
  ): Promise<ZLinkSpotCreateResult>;
  get(spotRid: string): Promise<ZLinkSpotInfo | undefined>;
  list(): Promise<readonly ZLinkSpotInfo[]>;
  remove(spotRid: string): Promise<boolean>;
}

export interface ZLinkSpot {
  readonly spotRid: string;
  addTimer(
    name: string,
    periodMs: number,
    handlerType: Function
  ): Promise<ZLinkTimer>;
}

export interface SpotRouterConnections {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
  listConnections(): readonly string[];
}

export interface SpotPubSubConnections {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
  listConnections(): readonly string[];
}

export interface SpotChannelClientConnections {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
  listConnections(): readonly string[];
}

export interface SpotPublisherClientConnections {
  connect(endpoint: string): void;
  disconnect(endpoint: string): void;
  listConnections(): readonly string[];
}

export interface ZLinkSpotConnectionManager {
  getRouter(spotNodeName: string): SpotRouterConnections;
  getPubSub(spotNodeName: string): SpotPubSubConnections;
  getChannelClient(
    spotNodeName: string,
    channelName: string
  ): SpotChannelClientConnections;
  getSpotPublisherClient(
    spotNodeName: string,
    channelName: string
  ): SpotPublisherClientConnections;
}

export interface ZLinkTimer {
  readonly isDisposed: boolean;
  cancel(): Promise<void>;
}
```

일반 channel client manual 연결은 endpoint 집합만 다루고, `SPOT` router manual
연결도 같은 방식으로 endpoint 집합만 등록한다. 이 초안에서는 `connect(...)`
호출 시 remote router id를 따로 받지 않는다. `ZLinkSpotManager`는 등록된
`spotName`으로 factory를 고르고, `get(...)`와 `list(...)`는 runtime이 들고 있는
`spotRid -> spotName` 매핑을 다시 보는 용도다.

## 3. Decorator

```ts
export function ZLinkPacket(packetName: string): ClassDecorator;
export function ZLinkRequest(packetName?: string): MethodDecorator;
export function ZLinkSend(packetName?: string): MethodDecorator;
export function ZLinkEvent(packetName?: string): MethodDecorator;
```

packet key 해석 규칙은 아래 순서를 기본으로 본다.

1. `options.packetName`
2. payload 타입 `@ZLinkPacket`
3. payload constructor 또는 schema 이름

## 4. Handler

```ts
export interface ZLinkRequestHandler<TReq, TRep> {
  handle(
    request: TReq,
    context: ZLinkRequestContext
  ): Promise<TRep>;
}

export interface ZLinkSendHandler<TMsg> {
  handle(
    message: TMsg,
    context: ZLinkSendContext
  ): Promise<void>;
}

export interface ZLinkEventHandler<TEvent> {
  handle(
    event: TEvent,
    context: ZLinkEventContext
  ): Promise<void>;
}
```

## 5. 중요한 규칙

- 같은 capability는 자동 연결과 수동 연결 중 하나만 선택한다.
- 수동 연결은 `channel + capability` 단위로 관리한다.
- manual capability는 startup 등록뿐 아니라 런타임 `connect`, `disconnect`,
  `listConnections`도 지원해야 한다.
- 일반 channel messaging의 handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
