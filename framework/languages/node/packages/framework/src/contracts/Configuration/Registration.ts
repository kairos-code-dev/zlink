import type {
  RoutingId,
  Type,
  ZLinkClientServerChannelBuilder,
  ZLinkDiscoveryBuilder,
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkFanoutChannelBuilder,
  ZLinkFrameworkOptions,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkSendContext,
  ZLinkRouteChannelBuilder,
  ZLinkRouteMeshChannelBuilder,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkHandlerFilter,
  ZLinkMonitoringOptions,
  ZLinkSpot,
  ZLinkSpotMeshBuilder,
  ZLinkSpotNodeBuilder,
  ZLinkStreamCompressionBuilder,
  ZLinkStreamCompressionCodec,
  ZLinkStreamCompressionOptions,
  ZLinkStreamNodeBuilder,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkSocketConfig,
  ZLinkTimerOptions
} from '../../contracts';
import type {
  ZLinkCodecExtension,
  ZLinkCodecRegistrar,
  ZLinkCodecRegistryBuilder,
  ZLinkMessageSerializer
} from '../Codecs';
import type {
  ZLinkDispatchOptions,
  ZLinkDispatchOptionsBuilder,
  ZLinkMessageFlowLogMode,
  ZLinkMessageFlowObserver
} from '../Dispatch';
import { validateFrameworkRegistration } from './RegistrationValidators';
export { validateFrameworkRegistration };

export interface ZLinkFrameworkRegistration {
  readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly codecs: ZLinkCodecRegistration;
  readonly requestTimeoutMs?: number;
  readonly actorFactories: ReadonlyMap<string, Type>;
  readonly spotFactories: ReadonlySet<Type<ZLinkSpot>>;
  readonly channels: ReadonlyMap<string, ZLinkChannelOptions>;
  readonly channelClients: ReadonlySet<string>;
  readonly fanoutPublishers: ReadonlySet<string>;
  readonly routeChannels: ReadonlySet<string>;
  readonly routeChannelOptions: ReadonlyMap<string, ZLinkRouteChannelOptions>;
  readonly streamNodes: ReadonlyMap<string, ZLinkStreamNodeOptions>;
  readonly streamCompression?: ZLinkStreamCompressionOptions;
  readonly spotNodes: ReadonlyMap<string, ZLinkSpotNodeOptions>;
  readonly discovery?: ZLinkDiscoveryOptions;
  readonly spotPublisherClients: ReadonlySet<string>;
  readonly hasSpotRemoteAddressResolver: boolean;
  readonly hasRegistrySpotRemoteAddresses: boolean;
  readonly spotRemoteAddressResolverType?: Type;
  readonly registrySpotRemoteAddresses?: ZLinkRegistrySpotRemoteAddressesRegistration;
  readonly filterTypes: readonly Type<ZLinkHandlerFilter>[];
  readonly worker?: ZLinkWorkerOptions;
  readonly dispatch?: ZLinkDispatchOptions;
  readonly monitoring?: ZLinkMonitoringOptions;
}

/**
 * Worker offload pool settings (single elastic bounded pool semantics).
 *
 * The Node runtime projects these settings conservatively onto the
 * closure-based `runWorker(...)` deferral: `maxThreads` bounds the number of
 * concurrently in-flight jobs and `maxQueueLength` bounds the pending queue
 * (queue full fails the submit with `WorkerQueueFull`). `minThreads` and
 * `idleTimeoutMs` are accepted and validated for cross-language option parity.
 */
export interface ZLinkWorkerOptions {
  readonly minThreads?: number;
  readonly maxThreads?: number;
  readonly idleTimeoutMs?: number;
  readonly maxQueueLength?: number;
}

export interface ZLinkRegistrySpotRemoteAddressesRegistration {
  readonly namespace: string;
  readonly routerChannelId?: string;
  readonly registryEndpoint: string;
}

export interface ZLinkCodecSerializerRegistration {
  readonly contentType: string;
  readonly serializer: ZLinkMessageSerializer;
}

export interface ZLinkStreamCodecRegistration {
  readonly contentType: string;
  readonly codec: unknown;
}

export interface ZLinkCodecRegistration {
  readonly serializers: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly streamCodecs: ReadonlyMap<string, unknown>;
}

export interface ZLinkCodecRegistryOptions {
  readonly serializers?: readonly ZLinkCodecSerializerRegistration[];
  readonly streamCodecs?: readonly ZLinkStreamCodecRegistration[];
}

export interface ZLinkFrameworkRegistrationOptions {
  readonly codecs?: ZLinkCodecRegistryOptions;
  readonly requestTimeoutMs?: number;
  readonly spotFactories?: readonly Type<ZLinkSpot>[];
  readonly channels?: Readonly<Record<string, ZLinkChannelOptions>>;
  readonly discovery?: ZLinkDiscoveryOptions;
  readonly routeChannels?: readonly (string | ZLinkRouteChannelOptions)[];
  readonly streamNodes?: Readonly<Record<string, ZLinkStreamNodeOptions>>;
  readonly streamCompression?: ZLinkStreamCompressionOptions;
  readonly spotNodes?: readonly (string | ZLinkSpotNodeRegistrationOptions)[] |
    Readonly<Record<string, ZLinkSpotNodeOptions>>;
  readonly spotPublisherClients?: readonly string[];
  readonly spotRemoteAddressResolver?: Type;
  readonly registrySpotRemoteAddresses?: {
    readonly namespace: string;
    readonly routerChannelId?: string;
  };
  readonly filters?: readonly Type<ZLinkHandlerFilter>[];
  readonly worker?: ZLinkWorkerOptions;
  readonly dispatch?: ZLinkDispatchOptions;
  readonly monitoring?: ZLinkMonitoringOptions;
}

export interface ZLinkDiscoveryOptions {
  readonly registries?: readonly string[];
}

export interface ZLinkChannelOptions {
  readonly requestTimeoutMs?: number;
  readonly client?: ZLinkClientCapabilityOptions;
  readonly publisher?: ZLinkPublisherCapabilityOptions;
  readonly routeMesh?: ZLinkRouteMeshChannelOptions;
  readonly publishHandlers?: readonly ZLinkChannelPublishHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkChannelRequestHandlerRegistration[];
  readonly sendHandlers?: readonly ZLinkChannelSendHandlerRegistration[];
  readonly server?: {
    readonly bind?: string;
    readonly routingId?: string;
    readonly weight?: number;
    readonly sendHighWaterMark?: number;
    readonly receiveHighWaterMark?: number;
    readonly sendTimeoutMs?: number;
  };
  readonly subscriber?: ZLinkClientCapabilityOptions;
}

export interface ZLinkClientCapabilityOptions {
  readonly manualConnections?: readonly string[];
  readonly sendHighWaterMark?: number;
  readonly receiveHighWaterMark?: number;
  readonly sendTimeoutMs?: number;
}

export interface ZLinkPublisherCapabilityOptions {
  readonly bind?: string;
}

export interface ZLinkRouteMeshChannelOptions {
  readonly requestTimeoutMs?: number;
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly weight?: number;
  readonly sendHighWaterMark?: number;
  readonly receiveHighWaterMark?: number;
  readonly sendTimeoutMs?: number;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: readonly ZLinkRouteChannelHandlerOptions[];
}

export interface ZLinkRouteChannelOptions {
  readonly routerChannelId: string;
  readonly requestTimeoutMs?: number;
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly weight?: number;
  readonly sendHighWaterMark?: number;
  readonly receiveHighWaterMark?: number;
  readonly sendTimeoutMs?: number;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: readonly ZLinkRouteChannelHandlerOptions[];
}

export interface ZLinkStreamNodeOptions {
  readonly bind?: string;
  readonly tlsServer?: ZLinkStreamTlsServerOptions;
  readonly session?: Type;
}

export interface ZLinkStreamTlsServerOptions {
  readonly certificatePath: string;
  readonly keyPath: string;
  readonly requireClientCertificate?: boolean;
}

export interface ZLinkSpotNodeRegistrationOptions extends ZLinkSpotNodeOptions {
  readonly name: string;
}

export interface ZLinkSpotNodeOptions {
  readonly routingId?: string;
  readonly router?: ZLinkSpotRouterCapabilityOptions;
  readonly pubSub?: ZLinkSpotPubSubCapabilityOptions;
  readonly entrySpot?: ZLinkEntrySpotOptions;
  readonly entrySpotType?: Type<ZLinkEntrySpot>;
  readonly spotFactories?: readonly Type<ZLinkSpot>[];
  readonly actorFactories?: Readonly<Record<string, Type> | Map<string, Type>>;
  readonly entrySpotTimerHandlers?: readonly ZLinkEntrySpotTimerHandlerRegistration[];
  readonly entrySpotActorSendHandlers?: readonly ZLinkEntrySpotActorSendHandlerRegistration[];
  readonly entrySpotActorRequestHandlers?: readonly ZLinkEntrySpotActorRequestHandlerRegistration[];
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
}

export interface ZLinkEntrySpotTimerHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly handlerType: Type;
  readonly name: string;
  readonly periodMs: number;
  readonly options?: ZLinkTimerOptions;
}

export interface ZLinkEntrySpotActorSendHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly actorType: Type<ZLinkActor>;
  readonly handlerType: Type;
  readonly packetName: string;
}

export interface ZLinkEntrySpotActorRequestHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly actorType: Type<ZLinkActor>;
  readonly handlerType: Type;
  readonly packetName: string;
}

export interface ZLinkSpotTimerHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>;
  readonly handlerType: Type;
  readonly name: string;
  readonly periodMs: number;
  readonly options?: ZLinkTimerOptions;
}

export interface ZLinkSpotActorSendHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>;
  readonly actorType: Type<ZLinkActor>;
  readonly handlerType: Type;
  readonly packetName: string;
}

export interface ZLinkSpotActorRequestHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>;
  readonly actorType: Type<ZLinkActor>;
  readonly handlerType: Type;
  readonly packetName: string;
}

export interface ZLinkSpotRouterCapabilityOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly manualPeerConnections?: readonly ZLinkSpotRouterPeerConnectionOptions[];
  readonly routingId?: string;
}

export interface ZLinkSpotRouterPeerConnectionOptions {
  readonly peerRid: RoutingId;
  readonly endpoint: string;
}

export interface ZLinkSpotPubSubCapabilityOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
}

export interface ZLinkRouteChannelHandlerOptions {
  readonly kind: 'send' | 'request';
  readonly packetName: string;
  readonly handler: ZLinkRouteChannelSendHandler | ZLinkRouteChannelRequestHandler;
}

export interface ZLinkRouteChannelSendHandlerRegistration {
  readonly packetName: string;
  readonly handler: ZLinkRouteChannelSendHandler;
}

export interface ZLinkRouteChannelRequestHandlerRegistration {
  readonly packetName: string;
  readonly handler: ZLinkRouteChannelRequestHandler;
}

export interface ZLinkChannelPublishHandlerRegistration {
  readonly packetName: string;
  readonly handler: ZLinkChannelPublishHandler;
}

export interface ZLinkChannelRequestHandlerRegistration {
  readonly packetName: string;
  readonly handler: {
    handle(payload: unknown, context: ZLinkRequestContext): Promise<unknown> | unknown;
  };
}

export interface ZLinkChannelSendHandlerRegistration {
  readonly packetName: string;
  readonly handler: {
    handle(payload: unknown, context: ZLinkSendContext): Promise<void> | void;
  };
}

export interface ZLinkChannelPublishHandler {
  handle(payload: unknown, context: ZLinkPublishContext): Promise<void> | void;
}

export interface ZLinkRouteChannelSendHandler {
  handle(payload: unknown, context: ZLinkRouteSendContext): Promise<void> | void;
}

export interface ZLinkRouteChannelRequestHandler {
  handle(payload: unknown, context: ZLinkRouteRequestContext): Promise<unknown> | unknown;
}

export class ZLinkConfigurationException extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ZLinkConfigurationException';
  }
}

export function createFrameworkRegistration(
  options: ZLinkFrameworkRegistrationOptions = {}
): ZLinkFrameworkRegistration {
  const codecRegistry = createCodecRegistry(options.codecs);
  const routeChannelOptions = toRouteChannelOptions(options);
  const spotNodes = toSpotNodeMap(options.spotNodes);
  const registration: ZLinkFrameworkRegistration = {
    messageSerializers: codecRegistry.registeredSerializers,
    codecs: codecRegistry.registration,
    requestTimeoutMs: normalizeOptionalPositiveInteger(options.requestTimeoutMs, 'requestTimeoutMs'),
    actorFactories: actorFactoriesFromSpotNodes(spotNodes),
    spotFactories: toSpotFactorySet(options.spotFactories, spotNodes),
    channels: toChannelMap(options.channels),
    channelClients: channelNamesWith(options.channels, (channel) => channel.client !== undefined),
    fanoutPublishers: channelNamesWith(options.channels, (channel) => channel.publisher !== undefined),
    routeChannels: new Set(routeChannelOptions.keys()),
    routeChannelOptions,
    streamNodes: toStreamNodeMap(options.streamNodes),
    streamCompression: normalizeStreamCompression(options.streamCompression),
    spotNodes,
    discovery: options.discovery,
    spotPublisherClients: toSpotPublisherClientSet(options.spotPublisherClients, spotNodes),
    hasSpotRemoteAddressResolver: options.spotRemoteAddressResolver !== undefined,
    hasRegistrySpotRemoteAddresses: options.registrySpotRemoteAddresses !== undefined,
    spotRemoteAddressResolverType: options.spotRemoteAddressResolver,
    registrySpotRemoteAddresses: normalizeRegistrySpotRemoteAddresses(options.registrySpotRemoteAddresses, options.discovery),
    filterTypes: [...(options.filters ?? [])],
    worker: options.worker === undefined ? undefined : { ...options.worker },
    dispatch: options.dispatch === undefined ? undefined : { ...options.dispatch },
    monitoring: options.monitoring === undefined ? undefined : { ...options.monitoring }
  };
  validateFrameworkRegistration(registration, options);
  return registration;
}

export function createFrameworkOptions(
  configure: (options: ZLinkFrameworkOptions) => void
): ZLinkFrameworkRegistrationOptions {
  const builder = new ZLinkFrameworkOptionsBuilder();
  configure(builder);
  return builder.build();
}

export function createFrameworkRegistrationWithBuilder(
  configure: (options: ZLinkFrameworkOptions) => void
): ZLinkFrameworkRegistration {
  return createFrameworkRegistration(createFrameworkOptions(configure));
}

class ZLinkFrameworkOptionsBuilder implements ZLinkFrameworkOptions {
  private readonly spotMeshes = new Set<string>();
  private readonly options: MutableFrameworkRegistrationOptions = {
    channels: {},
    discovery: { registries: [] },
    routeChannels: [],
    streamNodes: {},
    spotNodes: {},
    spotFactories: []
  };

  useDiscovery(): ZLinkDiscoveryBuilder {
    return new DefaultDiscoveryBuilder(this.options.discovery);
  }

  codecs(): ZLinkCodecRegistryBuilder {
    this.options.codecs ??= { serializers: [], streamCodecs: [] };
    return new RegistrationCodecRegistryBuilder(this.options.codecs);
  }

  configureWorker(options: ZLinkWorkerOptions): this {
    this.options.worker = { ...this.options.worker, ...options };
    return this;
  }

  configureDispatch(): ZLinkDispatchOptionsBuilder {
    this.options.dispatch ??= {};
    return new DefaultDispatchOptionsBuilder(this.options.dispatch);
  }

  configureStreamCompression(): ZLinkStreamCompressionBuilder {
    this.options.streamCompression ??= {};
    return new DefaultStreamCompressionBuilder(this.options.streamCompression);
  }

  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this {
    this.options.spotFactories.push(spotType);
    return this;
  }

  addSpotMesh(channelName: string): ZLinkSpotMeshBuilder {
    if (channelName.trim().length === 0 || channelName.trim() !== channelName) {
      throw new ZLinkConfigurationException('SPOT mesh channel name must not be empty or padded.');
    }
    if (this.spotMeshes.has(channelName)) {
      throw new ZLinkConfigurationException(`Duplicate SPOT mesh channel '${channelName}'.`);
    }
    this.spotMeshes.add(channelName);
    const spotNode = this.spotNodeOptions(channelName);
    return new DefaultSpotMeshBuilder(spotNode);
  }

  addClientServerChannel(name: string): ZLinkClientServerChannelBuilder {
    return new DefaultClientServerChannelBuilder(this.channel(name));
  }

  addFanoutChannel(name: string): ZLinkFanoutChannelBuilder {
    return new DefaultFanoutChannelBuilder(this.channel(name));
  }

  addRouteChannel(name: string): ZLinkRouteChannelBuilder {
    const routeChannel: MutableRouteChannelOptions = { routerChannelId: name };
    this.options.routeChannels.push(routeChannel);
    return new DefaultRouteChannelBuilder(routeChannel);
  }

  addRouteMesh(name: string): ZLinkRouteMeshChannelBuilder {
    const channel = this.channel(name);
    channel.routeMesh ??= {};
    markRouteTransportDeclared(channel.routeMesh);
    return new DefaultRouteMeshChannelBuilder(channel.routeMesh);
  }

  addStreamNode(name: string): ZLinkStreamNodeBuilder {
    const streamNode = this.streamNodeOptions(name);
    return new DefaultStreamNodeBuilder(streamNode);
  }

  build(): ZLinkFrameworkRegistrationOptions {
    const discovery = this.options.discovery.registries.length === 0
      ? undefined
      : { registries: [...this.options.discovery.registries] };
    return {
      codecs: this.options.codecs,
      channels: this.options.channels,
      discovery,
      requestTimeoutMs: this.options.requestTimeoutMs,
      routeChannels: this.options.routeChannels,
      streamNodes: this.options.streamNodes,
      streamCompression: this.options.streamCompression,
      spotNodes: this.options.spotNodes,
      spotFactories: this.options.spotFactories,
      dispatch: this.options.dispatch,
      worker: this.options.worker
    };
  }

  private channel(name: string): MutableChannelOptions {
    this.options.channels[name] ??= {};
    return this.options.channels[name];
  }

  private streamNodeOptions(name: string): MutableStreamNodeOptions {
    this.options.streamNodes[name] ??= {};
    return this.options.streamNodes[name];
  }

  private spotNodeOptions(name: string): MutableSpotNodeOptions {
    this.options.spotNodes[name] ??= {};
    return this.options.spotNodes[name];
  }
}

class DefaultDispatchOptionsBuilder implements ZLinkDispatchOptionsBuilder {
  constructor(private readonly dispatch: ZLinkDispatchOptions) {}

  setMessageFlowObserver(observerType: Type<ZLinkMessageFlowObserver>): this {
    this.dispatch.messageFlowObserverType = observerType;
    return this;
  }

  messageFlow(mode: ZLinkMessageFlowLogMode): this {
    (this.dispatch.diagnostics ??= {}).messageFlowLogMode = mode;
    return this;
  }

  traceSampleRate(rate: number): this {
    (this.dispatch.diagnostics ??= {}).sampleRate = rate;
    return this;
  }

  includeMessageSizes(include: boolean): this {
    (this.dispatch.diagnostics ??= {}).includeMessageSizes = include;
    return this;
  }

  traceLogFile(path: string): this {
    (this.dispatch.diagnostics ??= {}).logFile = path;
    return this;
  }

  traceLabel(id: string): this {
    (this.dispatch.diagnostics ??= {}).label = id;
    return this;
  }
}

class DefaultClientServerChannelBuilder implements ZLinkClientServerChannelBuilder {
  constructor(private readonly channel: MutableChannelOptions) {}

  enableServer(endpoint: string): this {
    this.channel.server ??= {};
    this.channel.server.bind = endpoint;
    return this;
  }

  routingId(routingId: string): this {
    this.channel.server ??= {};
    this.channel.server.routingId = routingId;
    return this;
  }

  configureServerSocket(): ZLinkSocketConfig {
    this.channel.server ??= {};
    return this.channel.server;
  }

  configureClientSocket(): ZLinkSocketConfig {
    this.channel.client ??= { manualConnections: [] };
    return this.channel.client;
  }

  enableClient(endpoint?: string): this {
    this.channel.client ??= { manualConnections: [] };
    if (endpoint !== undefined) {
      this.channel.client.manualConnections ??= [];
      this.channel.client.manualConnections.push(endpoint);
    }
    return this;
  }

  setDefaultRequestTimeout(timeoutMs: number): this {
    this.channel.requestTimeoutMs = normalizeOptionalPositiveInteger(timeoutMs, 'requestTimeoutMs');
    return this;
  }
}

class DefaultFanoutChannelBuilder implements ZLinkFanoutChannelBuilder {
  constructor(private readonly channel: MutableChannelOptions) {}

  enablePublisher(endpoint: string): this {
    this.channel.publisher ??= {};
    this.channel.publisher.bind = endpoint;
    return this;
  }

  enableSubscriber(endpoint?: string): this {
    this.channel.subscriber ??= { manualConnections: [] };
    if (endpoint !== undefined) {
      this.channel.subscriber.manualConnections ??= [];
      this.channel.subscriber.manualConnections.push(endpoint);
    }
    return this;
  }
}

class DefaultRouteChannelBuilder implements ZLinkRouteChannelBuilder {
  constructor(private readonly routeChannel: MutableRouteChannelOptions) {}

  enableServer(endpoint: string): this {
    this.routeChannel.bind = endpoint;
    return this;
  }

  enableClient(endpoint?: string): this {
    markRouteClientEnabled(this.routeChannel);
    this.routeChannel.manualConnections ??= [];
    if (endpoint !== undefined) {
      this.routeChannel.manualConnections.push(endpoint);
    }
    return this;
  }

  configureSocket(): ZLinkSocketConfig {
    return this.routeChannel;
  }

  setDefaultRequestTimeout(timeoutMs: number): this {
    this.routeChannel.requestTimeoutMs = normalizeOptionalPositiveInteger(timeoutMs, 'requestTimeoutMs');
    return this;
  }
}

class DefaultRouteMeshChannelBuilder implements ZLinkRouteMeshChannelBuilder {
  constructor(private readonly routeMesh: MutableRouteMeshChannelOptions) {}

  enableServer(endpoint: string): this {
    this.routeMesh.bind = endpoint;
    return this;
  }

  enableClient(endpoint?: string): this {
    markRouteClientEnabled(this.routeMesh);
    this.routeMesh.manualConnections ??= [];
    if (endpoint !== undefined) {
      this.routeMesh.manualConnections.push(endpoint);
    }
    return this;
  }

  configureSocket(): ZLinkSocketConfig {
    return this.routeMesh;
  }

  setDefaultRequestTimeout(timeoutMs: number): this {
    this.routeMesh.requestTimeoutMs = normalizeOptionalPositiveInteger(timeoutMs, 'requestTimeoutMs');
    return this;
  }
}

class DefaultStreamNodeBuilder implements ZLinkStreamNodeBuilder {
  constructor(private readonly streamNode: MutableStreamNodeOptions) {}

  bind(endpoint: string): this {
    this.streamNode.bind = endpoint;
    return this;
  }

  setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate: boolean = false): this {
    this.streamNode.tlsServer = {
      certificatePath,
      keyPath,
      requireClientCertificate
    };
    return this;
  }

  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this {
    if (this.streamNode.session !== undefined) {
      throw new ZLinkConfigurationException('STREAM node cannot register more than one header stream session.');
    }
    this.streamNode.session = sessionType;
    return this;
  }
}

class DefaultStreamCompressionBuilder implements ZLinkStreamCompressionBuilder {
  constructor(private readonly options: MutableStreamCompressionOptions) {}

  useDefault(): this {
    this.options.disabled = false;
    this.options.codec = undefined;
    return this;
  }

  useLz4(): this {
    return this.useDefault();
  }

  use(codec: ZLinkStreamCompressionCodec): this {
    if (!isStreamCompressionCodec(codec)) {
      throw new ZLinkConfigurationException('STREAM compression codec must provide compress and decompress functions.');
    }
    this.options.disabled = false;
    this.options.codec = codec;
    return this;
  }

  disable(): this {
    this.options.disabled = true;
    this.options.codec = undefined;
    return this;
  }
}

class DefaultSpotMeshBuilder implements ZLinkSpotMeshBuilder {
  private readonly node: DefaultSpotNodeBuilder;

  constructor(
    spotNode: MutableSpotNodeOptions
  ) {
    this.node = new DefaultSpotNodeBuilder(spotNode);
  }

  routingId(routingId: RoutingId): this {
    this.node.routingId(routingId);
    return this;
  }

  enableRouter(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this {
    this.node.enableRouter(endpoint, routingId, connect);
    return this;
  }

  connectRouter(endpoint: string): this;
  connectRouter(peerRid: RoutingId, endpoint: string): this;
  connectRouter(peerRidOrEndpoint: RoutingId | string, endpoint?: string): this {
    if (endpoint === undefined) {
      this.node.connectRouter(peerRidOrEndpoint as string);
    } else {
      this.node.connectRouter(peerRidOrEndpoint, endpoint);
    }
    return this;
  }

  enablePubSub(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this {
    this.node.enablePubSub(endpoint, routingId, connect);
    return this;
  }

  connectPeerPub(endpoint: string): this {
    this.node.connectPeerPub(endpoint);
    return this;
  }

  configureEntrySpot(options: ZLinkEntrySpotOptions): this {
    this.node.configureEntrySpot(options);
    return this;
  }

  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this {
    this.node.addEntrySpot(entrySpotType);
    return this;
  }

  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this {
    this.node.addSpotFactory(spotType);
    return this;
  }

  actorFactory(actorType: string, factoryType: Type): this {
    this.node.actorFactory(actorType, factoryType);
    return this;
  }
}

class DefaultDiscoveryBuilder implements ZLinkDiscoveryBuilder {
  constructor(private readonly discovery: MutableDiscoveryOptions) {}

  addRegistryEndpoint(endpoint: string): this {
    this.discovery.registries.push(endpoint);
    return this;
  }
}

class DefaultSpotNodeBuilder implements ZLinkSpotNodeBuilder {
  constructor(private readonly spotNode: MutableSpotNodeOptions) {}

  routingId(routingId: RoutingId): this {
    this.spotNode.routingId = routingId;
    if (this.spotNode.router !== undefined) {
      this.spotNode.router.routingId = routingId;
    }
    if (this.spotNode.pubSub !== undefined) {
      this.spotNode.pubSub.routingId = routingId;
    }
    return this;
  }

  enableRouter(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this {
    this.spotNode.router = {
      ...(this.spotNode.router ?? {}),
      bind: endpoint,
      routingId: routingId ?? this.spotNode.routingId,
      manualConnections: connect === undefined ? this.spotNode.router?.manualConnections : endpointList(connect)
    };
    return this;
  }

  connectRouter(endpoint: string): this;
  connectRouter(peerRid: RoutingId, endpoint: string): this;
  connectRouter(peerRidOrEndpoint: RoutingId | string, endpoint?: string): this {
    this.spotNode.router ??= { manualConnections: [] };
    if (endpoint === undefined) {
      this.spotNode.router.manualConnections ??= [];
      this.spotNode.router.manualConnections.push(peerRidOrEndpoint as string);
      return this;
    }
    this.spotNode.router.manualPeerConnections ??= [];
    this.spotNode.router.manualPeerConnections.push({
      peerRid: peerRidOrEndpoint,
      endpoint
    });
    return this;
  }

  enablePubSub(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this {
    this.spotNode.pubSub = {
      ...(this.spotNode.pubSub ?? {}),
      bind: endpoint,
      routingId: routingId ?? this.spotNode.routingId,
      manualConnections: connect === undefined ? this.spotNode.pubSub?.manualConnections : endpointList(connect)
    };
    return this;
  }

  connectPeerPub(endpoint: string): this {
    this.spotNode.pubSub ??= { manualConnections: [] };
    this.spotNode.pubSub.manualConnections ??= [];
    this.spotNode.pubSub.manualConnections.push(endpoint);
    return this;
  }

  configureEntrySpot(options: ZLinkEntrySpotOptions): this {
    this.spotNode.entrySpot = { ...options };
    return this;
  }

  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this {
    if (this.spotNode.entrySpotType !== undefined) {
      throw new ZLinkConfigurationException('Duplicate Entry Spot registration on SpotNode.');
    }
    this.spotNode.entrySpotType = entrySpotType;
    return this;
  }

  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this {
    this.spotNode.spotFactories ??= [];
    if (this.spotNode.spotFactories.includes(spotType)) {
      throw new ZLinkConfigurationException('Duplicate SPOT factory registration on SpotNode.');
    }
    this.spotNode.spotFactories.push(spotType);
    return this;
  }

  actorFactory(actorType: string, factoryType: Type): this {
    const type = actorType.trim();
    if (type.length === 0 || type !== actorType) {
      throw new ZLinkConfigurationException('Actor factory type must not be empty or padded.');
    }
    const actorFactories = typeMapToRecord(this.spotNode.actorFactories);
    if (Object.hasOwn(actorFactories, type)) {
      throw new ZLinkConfigurationException(`Duplicate actor factory '${type}' on SpotNode.`);
    }
    actorFactories[type] = factoryType;
    this.spotNode.actorFactories = actorFactories;
    return this;
  }

}

function endpointList(endpoint: string | readonly string[]): string[] {
  return typeof endpoint === 'string' ? [endpoint] : [...endpoint];
}

interface MutableFrameworkRegistrationOptions {
  codecs?: MutableCodecRegistryOptions;
  channels: Record<string, MutableChannelOptions>;
  discovery: MutableDiscoveryOptions;
  routeChannels: MutableRouteChannelOptions[];
  streamNodes: Record<string, MutableStreamNodeOptions>;
  streamCompression?: MutableStreamCompressionOptions;
  spotNodes: Record<string, MutableSpotNodeOptions>;
  spotFactories: Type<ZLinkSpot>[];
  filters?: Type<ZLinkHandlerFilter>[];
  worker?: ZLinkWorkerOptions;
  dispatch?: ZLinkDispatchOptions;
  requestTimeoutMs?: number;
}

interface MutableCodecRegistryOptions {
  serializers: ZLinkCodecSerializerRegistration[];
  streamCodecs: ZLinkStreamCodecRegistration[];
}

function createCodecRegistry(options: ZLinkCodecRegistryOptions | undefined): RegistrationCodecRegistryBuilder {
  return new RegistrationCodecRegistryBuilder({
    serializers: [...(options?.serializers ?? [])],
    streamCodecs: [...(options?.streamCodecs ?? [])]
  });
}

class RegistrationCodecRegistryBuilder implements ZLinkCodecRegistryBuilder, ZLinkCodecRegistrar {
  constructor(private readonly options: MutableCodecRegistryOptions = { serializers: [], streamCodecs: [] }) {}

  get registeredSerializers(): ReadonlyMap<string, ZLinkMessageSerializer> {
    return new Map(this.options.serializers.map((entry) => [entry.contentType, entry.serializer]));
  }

  get registeredStreamCodecs(): ReadonlyMap<string, unknown> {
    return new Map(this.options.streamCodecs.map((entry) => [entry.contentType, entry.codec]));
  }

  get registration(): ZLinkCodecRegistration {
    return {
      serializers: this.registeredSerializers,
      streamCodecs: this.registeredStreamCodecs
    };
  }

  get registeredCodecs(): readonly string[] {
    return this.options.serializers.map((entry) => entry.contentType);
  }

  use(extension: ZLinkCodecExtension): this {
    extension.register(this);
    return this;
  }

  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this {
    const normalized = normalizeCodecContentType(contentType);
    const existing = this.options.serializers.findIndex((entry) => entry.contentType === normalized);
    const registration = { contentType: normalized, serializer };
    if (existing >= 0) {
      this.options.serializers[existing] = registration;
    } else {
      this.options.serializers.push(registration);
    }
    return this;
  }

  addStreamCodec(contentType: string, codec: unknown): this {
    const normalized = normalizeCodecContentType(contentType);
    const existing = this.options.streamCodecs.findIndex((entry) => entry.contentType === normalized);
    const registration = { contentType: normalized, codec };
    if (existing >= 0) {
      this.options.streamCodecs[existing] = registration;
    } else {
      this.options.streamCodecs.push(registration);
    }
    return this;
  }

}

function normalizeCodecContentType(contentType: string): string {
  const normalized = contentType.trim();
  if (normalized.length === 0) {
    throw new ZLinkConfigurationException('Codec content type must not be empty.');
  }
  return normalized;
}

interface MutableDiscoveryOptions {
  registries: string[];
}

interface MutableChannelOptions {
  requestTimeoutMs?: number;
  client?: MutableClientCapabilityOptions;
  publisher?: MutablePublisherCapabilityOptions;
  routeMesh?: MutableRouteMeshChannelOptions;
  publishHandlers?: ZLinkChannelPublishHandlerRegistration[];
  requestHandlers?: ZLinkChannelRequestHandlerRegistration[];
  sendHandlers?: ZLinkChannelSendHandlerRegistration[];
  server?: {
    bind?: string;
    routingId?: string;
    weight?: number;
    sendHighWaterMark?: number;
    receiveHighWaterMark?: number;
    sendTimeoutMs?: number;
  };
  subscriber?: MutableClientCapabilityOptions;
}

interface MutableClientCapabilityOptions {
  manualConnections?: string[];
  sendHighWaterMark?: number;
  receiveHighWaterMark?: number;
  sendTimeoutMs?: number;
}

interface MutablePublisherCapabilityOptions {
  bind?: string;
}

interface MutableRouteMeshChannelOptions {
  transportDeclared?: boolean;
  requestTimeoutMs?: number;
  bind?: string;
  clientEnabled?: boolean;
  manualConnections?: string[];
  routingId?: string;
  sendHighWaterMark?: number;
  receiveHighWaterMark?: number;
  sendTimeoutMs?: number;
  sendHandlers?: ZLinkRouteChannelSendHandlerRegistration[];
  requestHandlers?: ZLinkRouteChannelRequestHandlerRegistration[];
  handlers?: ZLinkRouteChannelHandlerOptions[];
}

interface MutableRouteChannelOptions extends MutableRouteMeshChannelOptions {
  routerChannelId: string;
}

interface MutableStreamNodeOptions {
  bind?: string;
  tlsServer?: ZLinkStreamTlsServerOptions;
  session?: Type;
}

interface MutableStreamCompressionOptions {
  disabled?: boolean;
  codec?: ZLinkStreamCompressionCodec;
}

interface MutableSpotNodeOptions {
  routingId?: string;
  router?: MutableSpotRouterCapabilityOptions;
  pubSub?: MutableSpotPubSubCapabilityOptions;
  entrySpot?: ZLinkEntrySpotOptions;
  entrySpotType?: Type<ZLinkEntrySpot>;
  spotFactories?: Type<ZLinkSpot>[];
  actorFactories?: Record<string, Type>;
}

interface MutableSpotRouterCapabilityOptions {
  bind?: string;
  manualConnections?: string[];
  manualPeerConnections?: ZLinkSpotRouterPeerConnectionOptions[];
  routingId?: string;
}

interface MutableSpotPubSubCapabilityOptions {
  bind?: string;
  manualConnections?: string[];
  routingId?: string;
}

function toChannelMap(channels: ZLinkFrameworkRegistrationOptions['channels']): Map<string, ZLinkChannelOptions> {
  return new Map(Object.entries(channels ?? {}).map(([name, channel]) => [name, {
    ...channel,
    requestTimeoutMs: normalizeOptionalPositiveInteger(channel.requestTimeoutMs, `${name}.requestTimeoutMs`)
  }]));
}

function toStreamNodeMap(streamNodes: ZLinkFrameworkRegistrationOptions['streamNodes']): Map<string, ZLinkStreamNodeOptions> {
  return new Map(Object.entries(streamNodes ?? {}).map(([name, streamNode]) => [name, { ...streamNode }]));
}

function toRouteChannelOptions(
  options: ZLinkFrameworkRegistrationOptions
): Map<string, ZLinkRouteChannelOptions> {
  const routeOptions = new Map<string, ZLinkRouteChannelOptions>();
  for (const routeChannel of options.routeChannels ?? []) {
    if (typeof routeChannel === 'string') {
      routeOptions.set(routeChannel, { routerChannelId: routeChannel });
      continue;
    }
    const normalized = {
      ...routeChannel,
      requestTimeoutMs: normalizeOptionalPositiveInteger(
        routeChannel.requestTimeoutMs,
        `${routeChannel.routerChannelId}.requestTimeoutMs`
      )
    } as ZLinkRouteChannelOptions;
    copyRouteInternalState(routeChannel, normalized);
    routeOptions.set(routeChannel.routerChannelId, normalized);
  }
  for (const [channelName, channel] of Object.entries(options.channels ?? {})) {
    if (channel.routeMesh === undefined) {
      continue;
    }
    if (routeOptions.has(channelName)) {
      throw new ZLinkConfigurationException(`Route mesh channel '${channelName}' is already registered.`);
    }
    const routeChannel = {
      routerChannelId: channelName,
      ...channel.routeMesh,
      requestTimeoutMs: normalizeOptionalPositiveInteger(
        channel.routeMesh.requestTimeoutMs,
        `${channelName}.routeMesh.requestTimeoutMs`
      )
    } as ZLinkRouteChannelOptions;
    markRouteTransportDeclared(routeChannel);
    copyRouteInternalState(channel.routeMesh, routeChannel);
    routeOptions.set(channelName, routeChannel);
  }
  return routeOptions;
}

interface RouteMeshInternalState {
  transportDeclared?: boolean;
  clientEnabled?: boolean;
}

function markRouteTransportDeclared(routeChannel: object): void {
  defineRouteInternalFlag(routeChannel, 'transportDeclared');
}

function markRouteClientEnabled(routeChannel: object): void {
  defineRouteInternalFlag(routeChannel, 'clientEnabled');
}

function copyRouteInternalState(source: object, target: object): void {
  const state = source as RouteMeshInternalState;
  if (state.transportDeclared === true) {
    markRouteTransportDeclared(target);
  }
  if (state.clientEnabled === true) {
    markRouteClientEnabled(target);
  }
}

function defineRouteInternalFlag(
  routeChannel: object,
  key: keyof RouteMeshInternalState
): void {
  Object.defineProperty(routeChannel, key, {
    value: true,
    configurable: true,
    enumerable: false,
    writable: true
  });
}


export function requirePositiveInteger(label: string, value: number | undefined): void {
  if (value === undefined) {
    return;
  }
  if (!Number.isInteger(value) || value <= 0) {
    throw new ZLinkConfigurationException(`${label} must be a positive integer.`);
  }
}

function normalizeOptionalPositiveInteger(value: number | undefined, label: string): number | undefined {
  requirePositiveInteger(label, value);
  return value;
}

export function hasSpotNode(registration: ZLinkFrameworkRegistration): boolean {
  return registration.spotNodes.size > 0;
}

export function hasActorManager(registration: ZLinkFrameworkRegistration): boolean {
  return hasSpotNode(registration) && registration.actorFactories.size > 0;
}

export function hasSpotPublisherClient(registration: ZLinkFrameworkRegistration): boolean {
  return registration.spotPublisherClients.size > 0;
}

export function hasSpotRemoteAddressResolver(registration: ZLinkFrameworkRegistration): boolean {
  return registration.hasSpotRemoteAddressResolver || registration.hasRegistrySpotRemoteAddresses;
}

function toTypeMap(value: ZLinkSpotNodeOptions['actorFactories']): Map<string, Type> {
  if (value === undefined) {
    return new Map();
  }
  if (value instanceof Map) {
    return new Map(value);
  }
  return new Map(Object.entries(value));
}

function typeMapToRecord(value: ZLinkSpotNodeOptions['actorFactories']): Record<string, Type> {
  if (value === undefined) {
    return {};
  }
  if (isTypeMap(value)) {
    return Object.fromEntries(value) as Record<string, Type>;
  }
  return { ...(value as Readonly<Record<string, Type>>) };
}

function isTypeMap(value: ZLinkSpotNodeOptions['actorFactories']): value is Map<string, Type> {
  return value instanceof Map;
}

function actorFactoriesFromSpotNodes(spotNodes: ReadonlyMap<string, ZLinkSpotNodeOptions>): Map<string, Type> {
  const actorCapableNodes = [...spotNodes.values()].filter((spotNode) => toTypeMap(spotNode.actorFactories).size > 0);
  if (actorCapableNodes.length === 0) {
    return new Map();
  }
  return toTypeMap(actorCapableNodes[0].actorFactories);
}

function toSpotNodeMap(value: ZLinkFrameworkRegistrationOptions['spotNodes']): Map<string, ZLinkSpotNodeOptions> {
  if (value === undefined) {
    return new Map();
  }
  if (!Array.isArray(value)) {
    return new Map(Object.entries(value).map(([name, spotNode]) => [name, normalizeSpotNodeOptions(spotNode)]));
  }
  return new Map(value.map((spotNode) => {
    if (typeof spotNode === 'string') {
      return [spotNode, {}];
    }
    const { name, ...options } = spotNode;
    return [name, normalizeSpotNodeOptions(options)];
  }));
}

function normalizeSpotNodeOptions(spotNode: ZLinkSpotNodeOptions): ZLinkSpotNodeOptions {
  if (spotNode.routingId === undefined) {
    return { ...spotNode };
  }
  return {
    ...spotNode,
    router: spotNode.router === undefined
      ? undefined
      : { ...spotNode.router, routingId: spotNode.router.routingId ?? spotNode.routingId },
    pubSub: spotNode.pubSub === undefined
      ? undefined
      : { ...spotNode.pubSub, routingId: spotNode.pubSub.routingId ?? spotNode.routingId }
  };
}

function toSpotFactorySet(
  rootFactories: readonly Type<ZLinkSpot>[] | undefined,
  spotNodes: ReadonlyMap<string, ZLinkSpotNodeOptions>
): Set<Type<ZLinkSpot>> {
  const factories = new Set(rootFactories ?? []);
  for (const spotNode of spotNodes.values()) {
    for (const spotFactory of spotNode.spotFactories ?? []) {
      factories.add(spotFactory);
    }
  }
  return factories;
}

function toSpotPublisherClientSet(
  explicitClients: readonly string[] | undefined,
  spotNodes: ReadonlyMap<string, ZLinkSpotNodeOptions>
): Set<string> {
  const clients = new Set(explicitClients ?? []);
  for (const [spotNodeName, spotNode] of spotNodes.entries()) {
    if (spotNode.pubSub !== undefined) {
      clients.add(spotNodeName);
    }
  }
  return clients;
}

function channelNamesWith(
  channels: ZLinkFrameworkRegistrationOptions['channels'],
  predicate: (channel: ZLinkChannelOptions) => boolean
): Set<string> {
  const names = new Set<string>();
  for (const [name, channel] of Object.entries(channels ?? {})) {
    if (predicate(channel)) {
      names.add(name);
    }
  }
  return names;
}

function normalizeStreamCompression(
  value: ZLinkFrameworkRegistrationOptions['streamCompression']
): ZLinkStreamCompressionOptions | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (value.disabled === true && value.codec !== undefined) {
    throw new ZLinkConfigurationException('STREAM compression codec cannot be set when compression is disabled.');
  }
  if (value.codec !== undefined && !isStreamCompressionCodec(value.codec)) {
    throw new ZLinkConfigurationException('STREAM compression codec must provide compress and decompress functions.');
  }
  return {
    disabled: value.disabled,
    codec: value.codec
  };
}

function isStreamCompressionCodec(value: unknown): value is ZLinkStreamCompressionCodec {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { compress?: unknown }).compress === 'function'
    && typeof (value as { decompress?: unknown }).decompress === 'function';
}

function normalizeRegistrySpotRemoteAddresses(
  value: ZLinkFrameworkRegistrationOptions['registrySpotRemoteAddresses'],
  discovery: ZLinkDiscoveryOptions | undefined
): ZLinkRegistrySpotRemoteAddressesRegistration | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (value.namespace.trim().length === 0 || value.namespace.trim() !== value.namespace) {
    throw new ZLinkConfigurationException('Registry route namespace must not be empty or padded.');
  }
  if (value.routerChannelId !== undefined && (value.routerChannelId.trim().length === 0 || value.routerChannelId.trim() !== value.routerChannelId)) {
    throw new ZLinkConfigurationException('Registry route RouterChannelId must not be empty or padded.');
  }
  const registryEndpoint = (discovery?.registries ?? []).find((endpoint) => endpoint.trim().length > 0);
  return {
    namespace: value.namespace,
    routerChannelId: value.routerChannelId,
    registryEndpoint: registryEndpoint ?? ''
  };
}
