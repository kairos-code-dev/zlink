import type {
  RoutingId,
  Type,
  ZLinkClientServerChannelBuilder,
  ZLinkDealerMeshChannelBuilder,
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
  ZLinkSpot,
  ZLinkSpotMeshBuilder,
  ZLinkSpotMeshNodeBuilder,
  ZLinkSpotNodeBuilder,
  ZLinkStreamNodeBuilder,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkTimerOptions
} from '../../contracts';
import type { ZLinkCodecExtension, ZLinkCodecRegistryBuilder, ZLinkMessageSerializer } from '../Codecs';

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
  readonly spotNodes: ReadonlyMap<string, ZLinkSpotNodeOptions>;
  readonly discovery?: ZLinkDiscoveryOptions;
  readonly spotPublisherClients: ReadonlySet<string>;
  readonly hasSpotRemoteAddressResolver: boolean;
  readonly hasRegistrySpotRemoteAddresses: boolean;
  readonly spotRemoteAddressResolverType?: Type;
  readonly registrySpotRemoteAddresses?: ZLinkRegistrySpotRemoteAddressesRegistration;
  readonly worker?: ZLinkWorkerOptions;
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

export type ZLinkNamedCodec = 'json';

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
  readonly codecs?: readonly ZLinkNamedCodec[];
  readonly serializers?: readonly ZLinkCodecSerializerRegistration[];
  readonly streamCodecs?: readonly ZLinkStreamCodecRegistration[];
}

export interface ZLinkFrameworkRegistrationOptions {
  readonly codecs?: ZLinkCodecRegistryOptions;
  readonly requestTimeoutMs?: number;
  readonly actorFactories?: Readonly<Record<string, Type> | Map<string, Type>>;
  readonly spotFactories?: readonly Type<ZLinkSpot>[];
  readonly channels?: Readonly<Record<string, ZLinkChannelOptions>>;
  readonly discovery?: ZLinkDiscoveryOptions;
  readonly routeChannels?: readonly (string | ZLinkRouteChannelOptions)[];
  readonly streamNodes?: Readonly<Record<string, ZLinkStreamNodeOptions>>;
  readonly spotNodes?: readonly (string | ZLinkSpotNodeRegistrationOptions)[] |
    Readonly<Record<string, ZLinkSpotNodeOptions>>;
  readonly spotPublisherClients?: readonly string[];
  readonly spotRemoteAddressResolver?: Type;
  readonly registrySpotRemoteAddresses?: {
    readonly namespace: string;
    readonly routerChannelId?: string;
  };
  readonly worker?: ZLinkWorkerOptions;
}

export interface ZLinkDiscoveryOptions {
  readonly registries?: readonly string[];
}

export interface ZLinkChannelOptions {
  readonly client?: ZLinkClientCapabilityOptions;
  readonly dealerMesh?: ZLinkDealerMeshChannelOptions;
  readonly publisher?: ZLinkPublisherCapabilityOptions;
  readonly routeMesh?: ZLinkRouteMeshChannelOptions;
  readonly publishHandlers?: readonly ZLinkChannelPublishHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkChannelRequestHandlerRegistration[];
  readonly sendHandlers?: readonly ZLinkChannelSendHandlerRegistration[];
  readonly server?: { readonly bind?: string };
  readonly subscriber?: ZLinkClientCapabilityOptions;
}

export interface ZLinkClientCapabilityOptions {
  readonly manualConnections?: readonly string[];
}

export interface ZLinkDealerMeshChannelOptions {
  readonly bind?: string;
  readonly client?: ZLinkClientCapabilityOptions;
}

export interface ZLinkPublisherCapabilityOptions {
  readonly bind?: string;
}

export interface ZLinkRouteMeshChannelOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: readonly ZLinkRouteChannelHandlerOptions[];
}

export interface ZLinkRouteChannelOptions {
  readonly routerChannelId: string;
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: readonly ZLinkRouteChannelHandlerOptions[];
}

export interface ZLinkStreamNodeOptions {
  readonly bind?: string;
  readonly attachActorGateway?: string;
  readonly session?: Type;
}

export interface ZLinkSpotNodeRegistrationOptions extends ZLinkSpotNodeOptions {
  readonly name: string;
}

export interface ZLinkSpotNodeOptions {
  readonly router?: ZLinkSpotRouterCapabilityOptions;
  readonly pubSub?: ZLinkSpotPubSubCapabilityOptions;
  readonly entrySpot?: ZLinkEntrySpotOptions;
  readonly entrySpotType?: Type<ZLinkEntrySpot>;
  readonly spotFactories?: readonly Type<ZLinkSpot>[];
  readonly entrySpotTimerHandlers?: readonly ZLinkEntrySpotTimerHandlerRegistration[];
  readonly entrySpotActorSendHandlers?: readonly ZLinkEntrySpotActorSendHandlerRegistration[];
  readonly entrySpotActorRequestHandlers?: readonly ZLinkEntrySpotActorRequestHandlerRegistration[];
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
  readonly attachedChannelClients?: Readonly<Record<string, ZLinkSpotAttachedChannelClientOptions>>;
  readonly attachedSpotPublisherClients?: Readonly<Record<string, ZLinkSpotPublisherClientOptions>>;
  readonly acceptedSpotRouteChannels?: Readonly<Record<string, ZLinkSpotRouteChannelAcceptanceOptions>>;
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

export interface ZLinkSpotAttachedChannelClientOptions {
  readonly manualConnections?: readonly string[];
}

export interface ZLinkSpotPublisherClientOptions {
  readonly manualConnections?: readonly string[];
}

export interface ZLinkSpotRouteChannelAcceptanceOptions {
  readonly manualConnections?: readonly string[];
  readonly manualPeerConnections?: readonly ZLinkSpotRouterPeerConnectionOptions[];
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
    actorFactories: toTypeMap(options.actorFactories),
    spotFactories: toSpotFactorySet(options.spotFactories, spotNodes),
    channels: toChannelMap(options.channels),
    channelClients: channelNamesWith(options.channels, (channel) => channel.client !== undefined || channel.dealerMesh?.client !== undefined),
    fanoutPublishers: channelNamesWith(options.channels, (channel) => channel.publisher !== undefined),
    routeChannels: new Set(routeChannelOptions.keys()),
    routeChannelOptions,
    streamNodes: toStreamNodeMap(options.streamNodes),
    spotNodes,
    discovery: options.discovery,
    spotPublisherClients: toSpotPublisherClientSet(options.spotPublisherClients, spotNodes),
    hasSpotRemoteAddressResolver: options.spotRemoteAddressResolver !== undefined,
    hasRegistrySpotRemoteAddresses: options.registrySpotRemoteAddresses !== undefined,
    spotRemoteAddressResolverType: options.spotRemoteAddressResolver,
    registrySpotRemoteAddresses: normalizeRegistrySpotRemoteAddresses(options.registrySpotRemoteAddresses, options.discovery),
    worker: options.worker === undefined ? undefined : { ...options.worker }
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
    this.options.codecs ??= { codecs: [], serializers: [], streamCodecs: [] };
    return new RegistrationCodecRegistryBuilder(this.options.codecs);
  }

  configureWorker(options: ZLinkWorkerOptions): this {
    this.options.worker = { ...this.options.worker, ...options };
    return this;
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
    return new DefaultSpotMeshBuilder(this);
  }

  addClientServerChannel(name: string): ZLinkClientServerChannelBuilder {
    return new DefaultClientServerChannelBuilder(this.channel(name));
  }

  addFanoutChannel(name: string): ZLinkFanoutChannelBuilder {
    return new DefaultFanoutChannelBuilder(this.channel(name));
  }

  addDealerMeshChannel(name: string): ZLinkDealerMeshChannelBuilder {
    return new DefaultDealerMeshChannelBuilder(this.channel(name));
  }

  addRouteChannel(name: string): ZLinkRouteChannelBuilder {
    const routeChannel: MutableRouteChannelOptions = { routerChannelId: name };
    this.options.routeChannels.push(routeChannel);
    return new DefaultRouteChannelBuilder(routeChannel);
  }

  addRouteMeshChannel(name: string): ZLinkRouteMeshChannelBuilder {
    const channel = this.channel(name);
    channel.routeMesh ??= {};
    return new DefaultRouteMeshChannelBuilder(channel.routeMesh);
  }

  addStreamNode(name: string): ZLinkStreamNodeBuilder {
    const streamNode = this.streamNodeOptions(name);
    return new DefaultStreamNodeBuilder(streamNode);
  }

  addSpotNode(name: string): ZLinkSpotNodeBuilder {
    const spotNode = this.spotNodeOptions(name);
    return new DefaultSpotNodeBuilder(spotNode);
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
      spotNodes: this.options.spotNodes,
      spotFactories: this.options.spotFactories,
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

class DefaultClientServerChannelBuilder implements ZLinkClientServerChannelBuilder {
  constructor(private readonly channel: MutableChannelOptions) {}

  enableServer(endpoint: string): this {
    this.channel.server ??= {};
    this.channel.server.bind = endpoint;
    return this;
  }

  enableClient(endpoint?: string): this {
    this.channel.client ??= { manualConnections: [] };
    if (endpoint !== undefined) {
      this.channel.client.manualConnections ??= [];
      this.channel.client.manualConnections.push(endpoint);
    }
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

class DefaultDealerMeshChannelBuilder implements ZLinkDealerMeshChannelBuilder {
  constructor(private readonly channel: MutableChannelOptions) {}

  enableClient(endpoint?: string): this {
    this.channel.dealerMesh ??= {};
    this.channel.dealerMesh.client ??= { manualConnections: [] };
    if (endpoint !== undefined) {
      this.channel.dealerMesh.client.manualConnections ??= [];
      this.channel.dealerMesh.client.manualConnections.push(endpoint);
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
    this.routeChannel.manualConnections ??= [];
    if (endpoint !== undefined) {
      this.routeChannel.manualConnections.push(endpoint);
    }
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
    this.routeMesh.manualConnections ??= [];
    if (endpoint !== undefined) {
      this.routeMesh.manualConnections.push(endpoint);
    }
    return this;
  }
}

class DefaultStreamNodeBuilder implements ZLinkStreamNodeBuilder {
  constructor(private readonly streamNode: MutableStreamNodeOptions) {}

  bind(endpoint: string): this {
    this.streamNode.bind = endpoint;
    return this;
  }

  attachActorGateway(spotNodeName: string): this {
    this.streamNode.attachActorGateway = spotNodeName;
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

class DefaultSpotMeshBuilder implements ZLinkSpotMeshBuilder {
  constructor(private readonly root: ZLinkFrameworkOptionsBuilder) {}

  useDiscovery(): ZLinkDiscoveryBuilder {
    return this.root.useDiscovery();
  }

  addNode(name: string): ZLinkSpotMeshNodeBuilder {
    return this.root.addSpotNode(name);
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

  enableRouter(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this {
    this.spotNode.router = {
      ...(this.spotNode.router ?? {}),
      bind: endpoint,
      routingId,
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

  routerRoutingId(routingId: RoutingId): this {
    this.spotNode.router ??= { manualConnections: [] };
    this.spotNode.router.routingId = routingId;
    return this;
  }

  enablePubSub(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this {
    this.spotNode.pubSub = {
      ...(this.spotNode.pubSub ?? {}),
      bind: endpoint,
      routingId,
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

  connectPubSub(endpoint: string): this {
    return this.connectPeerPub(endpoint);
  }

  pubSubRoutingId(routingId: RoutingId): this {
    this.spotNode.pubSub ??= { manualConnections: [] };
    this.spotNode.pubSub.routingId = routingId;
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

  attachChannelClient(channelName: string, endpoint?: string | readonly string[]): this {
    this.spotNode.attachedChannelClients ??= {};
    this.spotNode.attachedChannelClients[channelName] ??= { manualConnections: [] };
    appendEndpoints(this.spotNode.attachedChannelClients[channelName], endpoint);
    return this;
  }

  attachSpotPublisherClient(channelName: string, endpoint?: string | readonly string[]): this {
    this.spotNode.attachedSpotPublisherClients ??= {};
    this.spotNode.attachedSpotPublisherClients[channelName] ??= { manualConnections: [] };
    appendEndpoints(this.spotNode.attachedSpotPublisherClients[channelName], endpoint);
    return this;
  }

  acceptSpotRoutesFromChannel(channelName: string, endpoint?: string | readonly string[]): this;
  acceptSpotRoutesFromChannel(channelName: string, peerRid: RoutingId, endpoint: string): this;
  acceptSpotRoutesFromChannel(channelName: string, peerRidOrEndpoint?: RoutingId | string | readonly string[], endpoint?: string): this {
    this.spotNode.acceptedSpotRouteChannels ??= {};
    this.spotNode.acceptedSpotRouteChannels[channelName] ??= { manualConnections: [] };
    if (endpoint !== undefined) {
      this.spotNode.acceptedSpotRouteChannels[channelName].manualPeerConnections ??= [];
      this.spotNode.acceptedSpotRouteChannels[channelName].manualPeerConnections!.push({
        peerRid: peerRidOrEndpoint as RoutingId,
        endpoint
      });
      return this;
    }
    appendEndpoints(
      this.spotNode.acceptedSpotRouteChannels[channelName],
      peerRidOrEndpoint as string | readonly string[] | undefined
    );
    return this;
  }
}

function endpointList(endpoint: string | readonly string[]): string[] {
  return typeof endpoint === 'string' ? [endpoint] : [...endpoint];
}

function appendEndpoints(
  capability: { manualConnections?: string[] },
  endpoint: string | readonly string[] | undefined
): void {
  if (endpoint === undefined) {
    return;
  }
  capability.manualConnections ??= [];
  capability.manualConnections.push(...endpointList(endpoint));
}

interface MutableFrameworkRegistrationOptions {
  codecs?: MutableCodecRegistryOptions;
  channels: Record<string, MutableChannelOptions>;
  discovery: MutableDiscoveryOptions;
  routeChannels: MutableRouteChannelOptions[];
  streamNodes: Record<string, MutableStreamNodeOptions>;
  spotNodes: Record<string, MutableSpotNodeOptions>;
  spotFactories: Type<ZLinkSpot>[];
  worker?: ZLinkWorkerOptions;
  requestTimeoutMs?: number;
}

interface MutableCodecRegistryOptions {
  codecs: ZLinkNamedCodec[];
  serializers: ZLinkCodecSerializerRegistration[];
  streamCodecs: ZLinkStreamCodecRegistration[];
}

function createCodecRegistry(options: ZLinkCodecRegistryOptions | undefined): RegistrationCodecRegistryBuilder {
  return new RegistrationCodecRegistryBuilder({
    codecs: [...(options?.codecs ?? [])],
    serializers: [...(options?.serializers ?? [])],
    streamCodecs: [...(options?.streamCodecs ?? [])]
  });
}

class RegistrationCodecRegistryBuilder implements ZLinkCodecRegistryBuilder {
  constructor(private readonly options: MutableCodecRegistryOptions = { codecs: [], serializers: [], streamCodecs: [] }) {}

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
    return [
      ...this.options.codecs,
      ...this.options.serializers.map((entry) => entry.contentType)
    ];
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

  addJson(): this {
    addNamedCodec(this.options, 'json');
    return this;
  }
}

function addNamedCodec(options: MutableCodecRegistryOptions, codec: ZLinkNamedCodec): void {
  if (!options.codecs.includes(codec)) {
    options.codecs.push(codec);
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
  client?: MutableClientCapabilityOptions;
  dealerMesh?: MutableDealerMeshChannelOptions;
  publisher?: MutablePublisherCapabilityOptions;
  routeMesh?: MutableRouteMeshChannelOptions;
  publishHandlers?: ZLinkChannelPublishHandlerRegistration[];
  requestHandlers?: ZLinkChannelRequestHandlerRegistration[];
  sendHandlers?: ZLinkChannelSendHandlerRegistration[];
  server?: { bind?: string };
  subscriber?: MutableClientCapabilityOptions;
}

interface MutableClientCapabilityOptions {
  manualConnections?: string[];
}

interface MutableDealerMeshChannelOptions {
  bind?: string;
  client?: MutableClientCapabilityOptions;
}

interface MutablePublisherCapabilityOptions {
  bind?: string;
}

interface MutableRouteMeshChannelOptions {
  bind?: string;
  manualConnections?: string[];
  routingId?: string;
  sendHandlers?: ZLinkRouteChannelSendHandlerRegistration[];
  requestHandlers?: ZLinkRouteChannelRequestHandlerRegistration[];
  handlers?: ZLinkRouteChannelHandlerOptions[];
}

interface MutableRouteChannelOptions extends MutableRouteMeshChannelOptions {
  routerChannelId: string;
}

interface MutableStreamNodeOptions {
  bind?: string;
  attachActorGateway?: string;
  session?: Type;
}

interface MutableSpotNodeOptions {
  router?: MutableSpotRouterCapabilityOptions;
  pubSub?: MutableSpotPubSubCapabilityOptions;
  entrySpot?: ZLinkEntrySpotOptions;
  entrySpotType?: Type<ZLinkEntrySpot>;
  spotFactories?: Type<ZLinkSpot>[];
  attachedChannelClients?: Record<string, MutableSpotAttachedChannelClientOptions>;
  attachedSpotPublisherClients?: Record<string, MutableSpotPublisherClientOptions>;
  acceptedSpotRouteChannels?: Record<string, MutableSpotRouteChannelAcceptanceOptions>;
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

interface MutableSpotAttachedChannelClientOptions {
  manualConnections?: string[];
}

interface MutableSpotPublisherClientOptions {
  manualConnections?: string[];
}

interface MutableSpotRouteChannelAcceptanceOptions {
  manualConnections?: string[];
  manualPeerConnections?: ZLinkSpotRouterPeerConnectionOptions[];
}

function toChannelMap(channels: ZLinkFrameworkRegistrationOptions['channels']): Map<string, ZLinkChannelOptions> {
  return new Map(Object.entries(channels ?? {}).map(([name, channel]) => [name, { ...channel }]));
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
    routeOptions.set(routeChannel.routerChannelId, { ...routeChannel });
  }
  for (const [channelName, channel] of Object.entries(options.channels ?? {})) {
    if (channel.routeMesh === undefined) {
      continue;
    }
    if (routeOptions.has(channelName)) {
      throw new ZLinkConfigurationException(`Route mesh channel '${channelName}' is already registered.`);
    }
    routeOptions.set(channelName, {
      routerChannelId: channelName,
      ...channel.routeMesh
    });
  }
  return routeOptions;
}

export function validateFrameworkRegistration(
  registration: ZLinkFrameworkRegistration,
  options: ZLinkFrameworkRegistrationOptions = {}
): void {
  if (registration.actorFactories.size > 0 && registration.spotNodes.size === 0) {
    throw new ZLinkConfigurationException('Actor factory registration requires at least one SpotNode.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && registration.routeChannels.size === 0) {
    throw new ZLinkConfigurationException('Registry remote address resolver requires a route mesh channel.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && registration.hasSpotRemoteAddressResolver) {
    throw new ZLinkConfigurationException('SPOT remote address resolver is already registered.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && !hasDiscovery(options.discovery)) {
    throw new ZLinkConfigurationException(
      'Registry remote address resolver requires discovery endpoints from discovery.registries.'
    );
  }

  validateRegistryRouteChannel(registration);
  validateChannelCapabilities(options.channels, hasDiscovery(options.discovery));
  validateSpotNodes(registration);
  validateRouteChannels(registration.routeChannelOptions);
  validateStreamNodes(registration);
  validateWorkerOptions(registration.worker);
}

function validateWorkerOptions(worker: ZLinkWorkerOptions | undefined): void {
  if (worker === undefined) {
    return;
  }
  requireNonNegativeInteger('Worker minThreads', worker.minThreads);
  requirePositiveInteger('Worker maxThreads', worker.maxThreads);
  requireNonNegativeInteger('Worker idleTimeoutMs', worker.idleTimeoutMs);
  requirePositiveInteger('Worker maxQueueLength', worker.maxQueueLength);
  if (
    worker.minThreads !== undefined
    && worker.maxThreads !== undefined
    && worker.minThreads > worker.maxThreads
  ) {
    throw new ZLinkConfigurationException('Worker minThreads must not exceed maxThreads.');
  }
}

function requireNonNegativeInteger(label: string, value: number | undefined): void {
  if (value === undefined) {
    return;
  }
  if (!Number.isInteger(value) || value < 0) {
    throw new ZLinkConfigurationException(`${label} must be a non-negative integer.`);
  }
}

function requirePositiveInteger(label: string, value: number | undefined): void {
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

function toTypeMap(value: ZLinkFrameworkRegistrationOptions['actorFactories']): Map<string, Type> {
  if (value === undefined) {
    return new Map();
  }
  if (value instanceof Map) {
    return new Map(value);
  }
  return new Map(Object.entries(value));
}

function toSpotNodeMap(value: ZLinkFrameworkRegistrationOptions['spotNodes']): Map<string, ZLinkSpotNodeOptions> {
  if (value === undefined) {
    return new Map();
  }
  if (!Array.isArray(value)) {
    return new Map(Object.entries(value).map(([name, spotNode]) => [name, { ...spotNode }]));
  }
  return new Map(value.map((spotNode) => {
    if (typeof spotNode === 'string') {
      return [spotNode, {}];
    }
    const { name, ...options } = spotNode;
    return [name, options];
  }));
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
  for (const spotNode of spotNodes.values()) {
    for (const channelName of Object.keys(spotNode.attachedSpotPublisherClients ?? {})) {
      clients.add(channelName);
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

function validateChannelCapabilities(
  channels: ZLinkFrameworkRegistrationOptions['channels'],
  discoveryConfigured: boolean
): void {
  for (const [channelName, channel] of Object.entries(channels ?? {})) {
    if (channel.server !== undefined) {
      requireEndpoint(`channel '${channelName}' server`, channel.server.bind);
    }
    if (channel.publisher !== undefined) {
      requireEndpoint(`channel '${channelName}' publisher`, channel.publisher.bind);
    }
    if (channel.client !== undefined) {
      requirePeerSource(`channel '${channelName}' client`, channel.client.manualConnections, discoveryConfigured);
    }
    if (channel.dealerMesh !== undefined) {
      if (channel.client !== undefined) {
        throw new ZLinkConfigurationException(`Channel '${channelName}' cannot define both client and dealerMesh client capability.`);
      }
      if (channel.dealerMesh.bind !== undefined) {
        requireEndpoint(`channel '${channelName}' dealer mesh`, channel.dealerMesh.bind);
      }
      requirePeerSource(`channel '${channelName}' dealer mesh client`, channel.dealerMesh.client?.manualConnections, discoveryConfigured);
    }
    if (channel.subscriber !== undefined) {
      requirePeerSource(`channel '${channelName}' subscriber`, channel.subscriber.manualConnections, discoveryConfigured);
    }
    if ((channel.publishHandlers ?? []).length > 0 && channel.subscriber === undefined) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' publish handlers require a subscriber capability.`
      );
    }
    if (((channel.requestHandlers ?? []).length > 0 || (channel.sendHandlers ?? []).length > 0) && channel.server === undefined) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' request/send handlers require a server capability.`
      );
    }
    validateDuplicatePacketNames(
      `channel '${channelName}' request handler`,
      channel.requestHandlers?.map((handler) => handler.packetName)
    );
    validateDuplicatePacketNames(
      `channel '${channelName}' send handler`,
      channel.sendHandlers?.map((handler) => handler.packetName)
    );
    validateDuplicatePacketNames(
      `channel '${channelName}' publish handler`,
      channel.publishHandlers?.map((handler) => handler.packetName)
    );
    if (channel.routeMesh !== undefined) {
      requireEndpoint(`channel '${channelName}' route mesh`, channel.routeMesh.bind);
      validateManualConnections(`channel '${channelName}' route mesh`, channel.routeMesh.manualConnections);
    }
  }
}

function validateSpotNodes(registration: ZLinkFrameworkRegistration): void {
  for (const [spotNodeName, spotNode] of registration.spotNodes.entries()) {
    if (spotNodeName.trim().length === 0 || spotNodeName.trim() !== spotNodeName) {
      throw new ZLinkConfigurationException('SpotNode name must not be empty or padded.');
    }
    validateSpotNodeCapability(`SpotNode '${spotNodeName}' router`, spotNode.router);
    validateSpotNodeCapability(`SpotNode '${spotNodeName}' pubSub`, spotNode.pubSub);
    validateAttachedChannelClients(spotNodeName, spotNode, registration);
    validateAttachedSpotPublisherClients(spotNodeName, spotNode);
    validateAcceptedSpotRouteChannels(spotNodeName, spotNode, registration);
    validateEntrySpot(spotNodeName, spotNode);
    validateSpotNodeFactories(spotNodeName, spotNode);
    if (
      spotNode.router?.routingId !== undefined &&
      spotNode.pubSub?.routingId !== undefined &&
      spotNode.router.routingId !== spotNode.pubSub.routingId
    ) {
      throw new ZLinkConfigurationException(
        `SpotNode '${spotNodeName}' router and pubSub routingId must match.`
      );
    }
  }
}

function validateEntrySpot(spotNodeName: string, spotNode: ZLinkSpotNodeOptions): void {
  if (spotNode.entrySpot?.routingId !== undefined) {
    requireName(`SpotNode '${spotNodeName}' Entry Spot routingId`, spotNode.entrySpot.routingId);
  }
}

function validateSpotNodeFactories(spotNodeName: string, spotNode: ZLinkSpotNodeOptions): void {
  const seen = new Set<Type<ZLinkSpot>>();
  for (const factory of spotNode.spotFactories ?? []) {
    if (seen.has(factory)) {
      throw new ZLinkConfigurationException(
        `Duplicate SPOT factory registration on SpotNode '${spotNodeName}'.`
      );
    }
    seen.add(factory);
  }
}

function validateAttachedChannelClients(
  spotNodeName: string,
  spotNode: ZLinkSpotNodeOptions,
  registration: ZLinkFrameworkRegistration
): void {
  for (const [channelName, attached] of Object.entries(spotNode.attachedChannelClients ?? {})) {
    requireName(`SpotNode '${spotNodeName}' attached channel client name`, channelName);
    if (registration.channels.get(channelName)?.server === undefined) {
      throw new ZLinkConfigurationException(
        `SpotNode '${spotNodeName}' attached channel client '${channelName}' must reference a client-server channel.`
      );
    }
    validateManualConnections(
      `SpotNode '${spotNodeName}' attached channel client '${channelName}'`,
      attached.manualConnections
    );
  }
}

function validateAttachedSpotPublisherClients(
  spotNodeName: string,
  spotNode: ZLinkSpotNodeOptions
): void {
  for (const [channelName, attached] of Object.entries(spotNode.attachedSpotPublisherClients ?? {})) {
    requireName(`SpotNode '${spotNodeName}' attached SPOT publisher channel name`, channelName);
    validateManualConnections(
      `SpotNode '${spotNodeName}' attached SPOT publisher '${channelName}'`,
      attached.manualConnections
    );
  }
}

function validateAcceptedSpotRouteChannels(
  spotNodeName: string,
  spotNode: ZLinkSpotNodeOptions,
  registration: ZLinkFrameworkRegistration
): void {
  for (const [channelName, acceptance] of Object.entries(spotNode.acceptedSpotRouteChannels ?? {})) {
    requireName(`SpotNode '${spotNodeName}' accepted SPOT route channel name`, channelName);
	    validateManualConnections(
	      `SpotNode '${spotNodeName}' accepted SPOT route channel '${channelName}'`,
	      acceptance.manualConnections
	    );
	    validateRouterPeerConnections(
	      `SpotNode '${spotNodeName}' accepted SPOT route channel '${channelName}'`,
	      acceptance.manualPeerConnections
	    );
    if (registration.routeChannelOptions.has(channelName)) {
      continue;
    }
    if (registration.channels.get(channelName)?.server !== undefined) {
      continue;
    }
    throw new ZLinkConfigurationException(
      `Accepted SPOT route channel '${channelName}' is not router-capable.`
    );
  }
}

function validateSpotNodeCapability(
  capabilityName: string,
  capability: ZLinkSpotRouterCapabilityOptions | ZLinkSpotPubSubCapabilityOptions | undefined
): void {
  if (capability === undefined) {
    return;
  }
  if (capability.bind !== undefined) {
    requireEndpoint(capabilityName, capability.bind);
  }
  validateManualConnections(capabilityName, capability.manualConnections);
  validateRouterPeerConnections(capabilityName, 'manualPeerConnections' in capability ? capability.manualPeerConnections : undefined);
  if (capability.routingId !== undefined && (capability.routingId.trim().length === 0 || capability.routingId.trim() !== capability.routingId)) {
    throw new ZLinkConfigurationException(`${capabilityName} routingId must not be empty or padded.`);
  }
}

function validateManualConnections(capabilityName: string, manualConnections: readonly string[] | undefined): void {
  if ((manualConnections ?? []).some((endpoint) => endpoint.trim().length === 0)) {
    throw new ZLinkConfigurationException(`${capabilityName} manual connection endpoint must not be empty.`);
  }
}

function validateRouterPeerConnections(
  capabilityName: string,
  manualPeerConnections: readonly ZLinkSpotRouterPeerConnectionOptions[] | undefined
): void {
  for (const connection of manualPeerConnections ?? []) {
    if (String(connection.peerRid).trim().length === 0) {
      throw new ZLinkConfigurationException(`${capabilityName} manual peer routing id must not be empty.`);
    }
    if (connection.endpoint.trim().length === 0) {
      throw new ZLinkConfigurationException(`${capabilityName} manual peer connection endpoint must not be empty.`);
    }
  }
}

function validateDuplicatePacketNames(label: string, packetNames: readonly string[] | undefined): void {
  const seen = new Set<string>();
  for (const packetName of packetNames ?? []) {
    requireName(label, packetName);
    if (seen.has(packetName)) {
      throw new ZLinkConfigurationException(`Duplicate ${label} packet '${packetName}'.`);
    }
    seen.add(packetName);
  }
}

function requireName(label: string, value: string): void {
  if (value.trim().length === 0 || value.trim() !== value) {
    throw new ZLinkConfigurationException(`${label} must not be empty or padded.`);
  }
}

function hasDiscovery(discovery: ZLinkDiscoveryOptions | undefined): boolean {
  return (discovery?.registries ?? []).some((endpoint) => endpoint.trim().length > 0);
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

function validateRegistryRouteChannel(registration: ZLinkFrameworkRegistration): void {
  if (!registration.hasRegistrySpotRemoteAddresses) {
    return;
  }

  const routerChannelId = registration.registrySpotRemoteAddresses?.routerChannelId;
  if (routerChannelId !== undefined) {
    if (!registration.routeChannels.has(routerChannelId)) {
      throw new ZLinkConfigurationException(
        `Registry SPOT remote address resolver references unknown route mesh channel '${routerChannelId}'.`
      );
    }
    return;
  }

  if (registration.routeChannels.size > 1) {
    throw new ZLinkConfigurationException(
      'Registry SPOT remote address resolver requires RouterChannelId when more than one route mesh channel is registered.'
    );
  }
}

function requirePeerSource(
  capabilityName: string,
  manualConnections: readonly string[] | undefined,
  discoveryConfigured: boolean
): void {
  validateManualConnections(capabilityName, manualConnections);
  if ((manualConnections ?? []).length > 0 || discoveryConfigured) {
    return;
  }
  throw new ZLinkConfigurationException(`${capabilityName} requires discovery or manual connections.`);
}

function requireEndpoint(capabilityName: string, endpoint: string | undefined): void {
  if (endpoint === undefined || endpoint.trim().length === 0) {
    throw new ZLinkConfigurationException(`${capabilityName} must define a bind endpoint.`);
  }
}

function validateStreamNodes(registration: ZLinkFrameworkRegistration): void {
  for (const [streamNodeName, streamNode] of registration.streamNodes.entries()) {
    requireEndpoint(`STREAM node '${streamNodeName}'`, streamNode.bind);
    if (streamNode.session === undefined) {
      throw new ZLinkConfigurationException(
        `STREAM node '${streamNodeName}' must register a header stream session.`
      );
    }
    if (streamNode.attachActorGateway === undefined || streamNode.attachActorGateway.trim().length === 0) {
      continue;
    }
    const spotNode = registration.spotNodes.get(streamNode.attachActorGateway);
    if (spotNode === undefined) {
      throw new ZLinkConfigurationException(
        `STREAM node '${streamNodeName}' references unknown ActorGateway target SpotNode '${streamNode.attachActorGateway}'.`
      );
    }
    if (spotNode.router === undefined) {
      throw new ZLinkConfigurationException(
        `STREAM node '${streamNodeName}' attaches ActorGateway to SpotNode '${streamNode.attachActorGateway}' but that SpotNode does not enable router capability.`
      );
    }
  }
}

function validateRouteChannels(routeChannels: ReadonlyMap<string, ZLinkRouteChannelOptions>): void {
  for (const routeChannel of routeChannels.values()) {
    if (routeChannelHandlerCount(routeChannel) > 0) {
      requireEndpoint(`route channel '${routeChannel.routerChannelId}' router`, routeChannel.bind);
    }
  }
}

function routeChannelHandlerCount(routeChannel: ZLinkRouteChannelOptions): number {
  return (routeChannel.handlers ?? []).length +
    (routeChannel.sendHandlers ?? []).length +
    (routeChannel.requestHandlers ?? []).length;
}
