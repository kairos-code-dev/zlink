import type {
  RoutingId,
  Type,
  ZLinkClientServerChannelBuilder,
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkFanoutChannelBuilder,
  ZLinkFrameworkOptions,
  ZLinkHandlerFilter,
  ZLinkRouteMeshChannelBuilder,
  ZLinkSpot,
  ZLinkActorTransferAdapter,
  ZLinkSpotMeshBuilder,
  ZLinkSpotNodeBuilder,
  ZLinkStreamCompressionBuilder,
  ZLinkStreamCompressionCodec,
  ZLinkStreamNodeBuilder,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkSocketConfig
} from '../../contracts';
import type { ZLinkCodecRegistryBuilder } from '../Codecs';
import type {
  ZLinkDispatchOptions,
  ZLinkDispatchOptionsBuilder,
  ZLinkMessageFlowObserver
} from '../Dispatch';
import { ZLinkMessageFlowLogMode, ZLinkUnhandledDispatchAction } from '../Dispatch';
import { setDispatchObserverType } from './DispatchObserverRegistration';
import { endpointConnections } from './RuntimeEndpointConnections';
import type { ZLinkEndpointConnections } from './Connections';
import type { ZLinkLocationStore, ZLinkLocationOptions } from '../Locations';
import { ZLinkConfigurationException } from './ConfigurationException';
import {
  RegistrationCodecRegistryBuilder,
  type MutableCodecRegistryOptions
} from './RegistrationCodecRegistry';
import {
  isStreamCompressionCodec,
  normalizeOptionalPositiveInteger,
  typeMapToRecord
} from './RegistrationNormalizers';
import {
  markRouteClientEnabled,
  markRouteTransportDeclared
} from './RouteChannelInternalState';
import type {
  ZLinkChannelPublishHandlerRegistration,
  ZLinkChannelRequestHandlerRegistration,
  ZLinkChannelSendHandlerRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkRouteChannelHandlerOptions,
  ZLinkRouteChannelRequestHandlerRegistration,
  ZLinkRouteChannelSendHandlerRegistration,
  ZLinkSpotRouterPeerConnectionOptions,
  ZLinkStreamTlsServerOptions,
  ZLinkWorkerOptions
} from './RegistrationTypes';
import {
  registerActorTransferAdapter,
  registerActorFactory,
  registerEntrySpot,
  registerSpotFactory,
  validateActorTransferForwardWindow
} from './RegistrationBuilderPolicy';

export function createFrameworkOptions(
  configure: (options: ZLinkFrameworkOptions) => void
): ZLinkFrameworkRegistrationOptions {
  const builder = new ZLinkFrameworkOptionsBuilder();
  configure(builder);
  return builder.build();
}

class ZLinkFrameworkOptionsBuilder implements ZLinkFrameworkOptions {
  private readonly spotMeshes = new Set<string>();
  private readonly options: MutableFrameworkRegistrationOptions = {
    actorTransferAdapters: new Map(),
    channels: {},
    routeChannels: [],
    streamNodes: {},
    spotNodes: {},
    spotFactories: []
  };

  codecs(): ZLinkCodecRegistryBuilder {
    this.options.codecs ??= { serializers: [], streamCodecs: [] };
    return new RegistrationCodecRegistryBuilder(this.options.codecs);
  }

  configureWorker(options: ZLinkWorkerOptions): this {
    this.options.worker = { ...this.options.worker, ...options };
    return this;
  }

  configureDispatch(): ZLinkDispatchOptionsBuilder {
    this.options.dispatch ??= defaultDispatchOptions();
    return new DefaultDispatchOptionsBuilder(this.options.dispatch);
  }

  useInMemoryLocationStores(): this {
    this.options.locations ??= { options: {} };
    this.options.locations.useInMemoryStores = true;
    return this;
  }

  addLocationStore(store: ZLinkLocationStore): this {
    this.options.locations ??= { options: {} };
    this.options.locations.storeInstance = store;
    return this;
  }

  addActorTransferAdapter<TActor extends ZLinkActor>(
    actorType: Type<TActor>,
    adapterType: Type<ZLinkActorTransferAdapter<TActor>>
  ): this {
    registerActorTransferAdapter(this.options.actorTransferAdapters, actorType, adapterType);
    return this;
  }

  setActorTransferForwardWindow(timeoutMs: number): this {
    this.options.actorTransferForwardWindowMs = validateActorTransferForwardWindow(timeoutMs);
    return this;
  }

  configureLocations(): ZLinkLocationOptions {
    this.options.locations ??= { options: {} };
    this.options.locations.options ??= {};
    return this.options.locations.options;
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
    return new DefaultSpotNodeBuilder(spotNode);
  }

  addClientServerChannel(name: string): ZLinkClientServerChannelBuilder {
    return new DefaultClientServerChannelBuilder(this.channel(name));
  }

  addFanoutChannel(name: string): ZLinkFanoutChannelBuilder {
    return new DefaultFanoutChannelBuilder(this.channel(name));
  }

  addRouteMeshChannel(name: string): ZLinkRouteMeshChannelBuilder {
    const channel = this.channel(name);
    channel.routeMesh ??= {};
    markRouteTransportDeclared(channel.routeMesh);
    return new DefaultRouteChannelOptionsBuilder(channel.routeMesh);
  }

  addStreamNode(name: string): ZLinkStreamNodeBuilder {
    if (name.trim().length === 0 || name.trim() !== name) {
      throw new ZLinkConfigurationException('STREAM node name must not be empty or padded.');
    }
    if (Object.prototype.hasOwnProperty.call(this.options.streamNodes, name)) {
      throw new ZLinkConfigurationException(`Duplicate STREAM node '${name}'.`);
    }
    const streamNode: MutableStreamNodeOptions = {};
    this.options.streamNodes[name] = streamNode;
    return new DefaultStreamNodeBuilder(streamNode);
  }

  build(): ZLinkFrameworkRegistrationOptions {
    return {
      codecs: this.options.codecs,
      channels: this.options.channels,
      requestTimeoutMs: this.options.requestTimeoutMs,
      routeChannels: this.options.routeChannels,
      streamNodes: this.options.streamNodes,
      streamCompression: this.options.streamCompression,
      spotNodes: this.options.spotNodes,
      spotFactories: this.options.spotFactories,
      actorTransferAdapters: new Map(this.options.actorTransferAdapters),
      actorTransferForwardWindowMs: this.options.actorTransferForwardWindowMs,
      dispatch: this.options.dispatch,
      worker: this.options.worker,
      locations: this.options.locations
    };
  }

  private channel(name: string): MutableChannelOptions {
    if (name.trim().length === 0 || name.trim() !== name) {
      throw new ZLinkConfigurationException('Channel name must not be empty or padded.');
    }
    if (Object.prototype.hasOwnProperty.call(this.options.channels, name)) {
      throw new ZLinkConfigurationException(`Duplicate channel '${name}'.`);
    }
    const channel: MutableChannelOptions = {};
    this.options.channels[name] = channel;
    return channel;
  }

  private spotNodeOptions(name: string): MutableSpotNodeOptions {
    this.options.spotNodes[name] ??= {};
    return this.options.spotNodes[name];
  }
}

export class DefaultDispatchOptionsBuilder implements ZLinkDispatchOptionsBuilder {
  constructor(private readonly dispatch: ZLinkDispatchOptions) {}

  setMessageFlowObserver(observerType: Type<ZLinkMessageFlowObserver>): this {
    setDispatchObserverType(this.dispatch, observerType);
    return this;
  }

  messageFlow(mode: ZLinkMessageFlowLogMode): this {
    this.dispatch.diagnostics.messageFlow = mode;
    return this;
  }

  traceSampleRate(rate: number): this {
    this.dispatch.diagnostics.sampleRate = rate;
    return this;
  }

  includeMessageSizes(include: boolean): this {
    this.dispatch.diagnostics.includeMessageSizes = include;
    return this;
  }

  traceLogFile(path: string): this {
    this.dispatch.diagnostics.logFile = path;
    return this;
  }

  traceLabel(id: string): this {
    this.dispatch.diagnostics.label = id;
    return this;
  }
}

function defaultDispatchOptions(): ZLinkDispatchOptions {
  return {
    unhandled: {
      request: ZLinkUnhandledDispatchAction.ReplyError,
      send: ZLinkUnhandledDispatchAction.LogAndDrop,
      publish: ZLinkUnhandledDispatchAction.LogAndDrop
    },
    diagnostics: {
      messageFlow: ZLinkMessageFlowLogMode.ErrorsOnly,
      sampleRate: 1,
      includeMessageSizes: false,
      includeNativeDiagnostics: false
    }
  };
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

  clientConnections(): ZLinkEndpointConnections {
    this.channel.client ??= { manualConnections: [] };
    this.channel.client.manualConnections ??= [];
    return endpointConnections(this.channel.client, this.channel.client.manualConnections);
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

  subscriberConnections(): ZLinkEndpointConnections {
    this.channel.subscriber ??= { manualConnections: [] };
    this.channel.subscriber.manualConnections ??= [];
    return endpointConnections(this.channel.subscriber, this.channel.subscriber.manualConnections);
  }
}

class DefaultRouteChannelOptionsBuilder<
  TOptions extends MutableRouteMeshChannelOptions
> implements ZLinkRouteMeshChannelBuilder {
  constructor(private readonly routeChannel: TOptions) {}

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

  clientConnections(): ZLinkEndpointConnections {
    this.routeChannel.manualConnections ??= [];
    return endpointConnections(this.routeChannel, this.routeChannel.manualConnections);
  }

  configureSocket(): ZLinkSocketConfig {
    return this.routeChannel;
  }

  setDefaultRequestTimeout(timeoutMs: number): this {
    this.routeChannel.requestTimeoutMs = normalizeOptionalPositiveInteger(timeoutMs, 'requestTimeoutMs');
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

export class DefaultStreamCompressionBuilder implements ZLinkStreamCompressionBuilder {
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
    registerEntrySpot(this.spotNode, entrySpotType);
    return this;
  }

  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this {
    registerSpotFactory(this.spotNode, spotType);
    return this;
  }

  actorFactory(actorType: string, factoryType: Type): this {
    this.spotNode.actorFactories = typeMapToRecord(this.spotNode.actorFactories);
    registerActorFactory(this.spotNode, actorType, factoryType);
    return this;
  }

  useDrainPolicy(policy: import('../Eventing').ZLinkSpotDrainPolicy): this {
    this.spotNode.drainPolicy = policy;
    return this;
  }
}

function endpointList(endpoint: string | readonly string[]): string[] {
  return typeof endpoint === 'string' ? [endpoint] : [...endpoint];
}

interface MutableFrameworkRegistrationOptions {
  actorTransferAdapters: Map<Type, Type>;
  actorTransferForwardWindowMs?: number;
  codecs?: MutableCodecRegistryOptions;
  channels: Record<string, MutableChannelOptions>;
  routeChannels: MutableRouteChannelOptions[];
  streamNodes: Record<string, MutableStreamNodeOptions>;
  streamCompression?: MutableStreamCompressionOptions;
  spotNodes: Record<string, MutableSpotNodeOptions>;
  spotFactories: Type<ZLinkSpot>[];
  filters?: Type<ZLinkHandlerFilter>[];
  worker?: ZLinkWorkerOptions;
  dispatch?: ZLinkDispatchOptions;
  requestTimeoutMs?: number;
  locations?: MutableLocationRegistrationOptions;
}

interface MutableLocationRegistrationOptions {
  useInMemoryStores?: boolean;
  storeInstance?: ZLinkLocationStore;
  options?: ZLinkLocationOptions;
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
    maxMessageSize?: number;
  };
  subscriber?: MutableClientCapabilityOptions;
}

interface MutableClientCapabilityOptions {
  manualConnections?: string[];
  sendHighWaterMark?: number;
  receiveHighWaterMark?: number;
  sendTimeoutMs?: number;
  maxMessageSize?: number;
}

interface MutablePublisherCapabilityOptions {
  bind?: string;
}

interface MutableRouteMeshChannelOptions {
  requestTimeoutMs?: number;
  bind?: string;
  manualConnections?: string[];
  routingId?: string;
  sendHighWaterMark?: number;
  receiveHighWaterMark?: number;
  sendTimeoutMs?: number;
  maxMessageSize?: number;
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

export interface MutableStreamCompressionOptions {
  disabled?: boolean;
  codec?: ZLinkStreamCompressionCodec;
}

interface MutableSpotNodeOptions {
  drainPolicy?: import('../Eventing').ZLinkSpotDrainPolicy;
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
