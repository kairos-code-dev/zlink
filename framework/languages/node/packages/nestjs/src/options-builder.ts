import type {
  Type,
  ZLinkActor,
  ZLinkActorTransferAdapter,
  ZLinkCodecExtension,
  ZLinkCodecRegistrar,
  ZLinkDispatchOptionsBuilder,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkFrameworkRegistrationOptions,
  ZLinkLocationStore,
  ZLinkLocationOptions,
  ZLinkMessageSerializer,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkSocketConfig,
  ZLinkSpot,
  ZLinkSpotNodeOptions,
  ZLinkStreamCompressionBuilder,
  ZLinkStreamNodeOptions
} from '@zlink-systems/framework/nest-integration';
import {
  ZLinkMessageFlowLogMode,
  ZLinkUnhandledDispatchAction
} from '@zlink-systems/framework';
import {
  ZLINK_MODULE_OPTIONS_BRAND,
  type Mutable,
  type MutableCodecRegistryOptions,
  type ZLinkModuleOptions,
  type ZLinkNestClientServerChannelBuilder,
  type InternalZLinkNestClientServerChannelOptions,
  type ZLinkNestCodecRegistryBuilder,
  type ZLinkNestFanoutChannelBuilder,
  type InternalZLinkNestFanoutChannelOptions,
  type ZLinkNestFrameworkAdditionalOptions,
  type ZLinkNestFrameworkOptionsBuilder,
  type ZLinkNestModuleRegistrationOptions,
  type ZLinkNestRouterMeshBuilder,
  type InternalZLinkNestRouterMeshOptions,
  type ZLinkNestSpotNodeBuilder,
  type ZLinkNestStreamNodeBuilder
} from './contracts';
import { framework } from './framework-loader';


interface ZLinkNestBuilderState {
  additionalOptions: ZLinkNestFrameworkAdditionalOptions;
  readonly clientServerChannels: Record<string, InternalZLinkNestClientServerChannelOptions>;
  readonly fanoutChannels: Record<string, InternalZLinkNestFanoutChannelOptions>;
  readonly routerMeshes: Record<string, InternalZLinkNestRouterMeshOptions>;
  readonly streams: Record<string, ZLinkStreamNodeOptions>;
  readonly spotNodes: Record<string, ZLinkSpotNodeOptions>;
  readonly codecOptions: MutableCodecRegistryOptions;
  readonly codecRegistry: ReturnType<typeof framework.createIntegrationCodecRegistryBuilder>;
}

function createBuilderState(): ZLinkNestBuilderState {
  const codecOptions: MutableCodecRegistryOptions = { serializers: [], streamCodecs: [] };
  return {
    additionalOptions: {},
    clientServerChannels: {},
    fanoutChannels: {},
    routerMeshes: {},
    streams: {},
    spotNodes: {},
    codecOptions,
    codecRegistry: framework.createIntegrationCodecRegistryBuilder(codecOptions)
  };
}

abstract class ZLinkNestOptionsBuilder implements ZLinkNestFrameworkOptionsBuilder {
  protected constructor(protected readonly state: ZLinkNestBuilderState) {}

  options(options: ZLinkNestFrameworkAdditionalOptions): this {
    this.state.additionalOptions = { ...this.state.additionalOptions, ...options };
    return this;
  }

  codecs(): ZLinkNestCodecRegistryBuilder {
    return new DefaultZLinkNestCodecRegistryBuilder(this.state);
  }

  configureDispatch(): ZLinkDispatchOptionsBuilder {
    this.state.additionalOptions = {
      ...this.state.additionalOptions,
      dispatch: this.state.additionalOptions.dispatch ?? {
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
      }
    };
    return framework.createIntegrationDispatchOptionsBuilder(
      this.state.additionalOptions.dispatch as NonNullable<ZLinkFrameworkRegistrationOptions['dispatch']>
    );
  }

  useInMemoryLocationStores(): this {
    this.state.additionalOptions = {
      ...this.state.additionalOptions,
      locations: {
        ...(this.state.additionalOptions.locations ?? {}),
        useInMemoryStores: true
      }
    };
    return this;
  }

  addLocationStore(store: ZLinkLocationStore): this {
    this.state.additionalOptions = {
      ...this.state.additionalOptions,
      locations: {
        ...(this.state.additionalOptions.locations ?? {}),
        storeInstance: store
      }
    };
    return this;
  }

  addActorTransferAdapter<TActor extends ZLinkActor>(
    actorType: Type<TActor>,
    adapterType: Type<ZLinkActorTransferAdapter<TActor>>
  ): this {
    const adapters = new Map(this.state.additionalOptions.actorTransferAdapters);
    framework.registerActorTransferAdapter(adapters, actorType, adapterType);
    this.state.additionalOptions = {
      ...this.state.additionalOptions,
      actorTransferAdapters: adapters
    };
    return this;
  }

  setActorTransferForwardWindow(timeoutMs: number): this {
    this.state.additionalOptions = {
      ...this.state.additionalOptions,
      actorTransferForwardWindowMs: framework.validateActorTransferForwardWindow(timeoutMs)
    };
    return this;
  }

  configureStreamCompression(): ZLinkStreamCompressionBuilder {
    const compression = { ...(this.state.additionalOptions.streamCompression ?? {}) };
    this.state.additionalOptions = { ...this.state.additionalOptions, streamCompression: compression };
    return framework.createIntegrationStreamCompressionBuilder(compression);
  }

  configureLocations(): ZLinkLocationOptions {
    this.state.additionalOptions = {
      ...this.state.additionalOptions,
      locations: this.state.additionalOptions.locations ?? { options: {} }
    };
    const locations = this.state.additionalOptions.locations as NonNullable<ZLinkFrameworkRegistrationOptions['locations']>;
    (locations as { options?: ZLinkLocationOptions }).options ??= {};
    return locations.options as ZLinkLocationOptions;
  }

  protected addSerializer(contentType: string, serializer: ZLinkMessageSerializer): void {
    this.state.codecRegistry.addSerializer(contentType, serializer);
  }

  protected addStreamCodec(contentType: string, codec: unknown): void {
    this.state.codecRegistry.addStreamCodec(contentType, codec);
  }

  addClientServerChannel(name: string): ZLinkNestClientServerChannelBuilder {
    this.ensureChannelAvailable(name);
    this.state.clientServerChannels[name] = {};
    return new DefaultZLinkNestClientServerChannelBuilder(this.state, this.state.clientServerChannels[name]);
  }

  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder {
    this.ensureChannelAvailable(name);
    this.state.fanoutChannels[name] = {};
    return new DefaultZLinkNestFanoutChannelBuilder(this.state, this.state.fanoutChannels[name]);
  }

  addRouteMeshChannel(name: string): ZLinkNestRouterMeshBuilder {
    this.ensureChannelAvailable(name);
    this.state.routerMeshes[name] = {};
    markRouteTransportDeclared(this.state.routerMeshes[name]);
    return new DefaultZLinkNestRouterMeshBuilder(this.state, this.state.routerMeshes[name]);
  }

  addSpotMesh(name: string): ZLinkNestSpotNodeBuilder {
    this.state.spotNodes[name] ??= {};
    return new DefaultZLinkNestSpotNodeBuilder(this.state, this.state.spotNodes[name]);
  }

  addStreamNode(name: string): ZLinkNestStreamNodeBuilder {
    this.state.streams[name] ??= {};
    return new DefaultZLinkNestStreamNodeBuilder(this.state, this.state.streams[name]);
  }

  private ensureChannelAvailable(name: string): void {
    if (name.trim().length === 0 || name.trim() !== name) {
      throw new framework.ZLinkConfigurationException('Channel name must not be empty or padded.');
    }
    if (
      Object.prototype.hasOwnProperty.call(this.state.clientServerChannels, name)
      || Object.prototype.hasOwnProperty.call(this.state.fanoutChannels, name)
      || Object.prototype.hasOwnProperty.call(this.state.routerMeshes, name)
    ) {
      throw new framework.ZLinkConfigurationException(`Duplicate channel '${name}'.`);
    }
  }

  build(): ZLinkModuleOptions {
    const options: ZLinkNestModuleRegistrationOptions = {
      [ZLINK_MODULE_OPTIONS_BRAND]: true,
      ...this.state.additionalOptions,
      clientServerChannels: { ...this.state.clientServerChannels },
      fanoutChannels: { ...this.state.fanoutChannels },
      routerMeshes: { ...this.state.routerMeshes },
      streams: { ...this.state.streams },
      spotNodes: { ...this.state.spotNodes },
      codecs: this.state.codecOptions.serializers.length === 0 &&
          this.state.codecOptions.streamCodecs.length === 0
        ? undefined
        : {
            serializers: [...this.state.codecOptions.serializers],
            streamCodecs: [...this.state.codecOptions.streamCodecs]
          }
    };
    return options;
  }
}

class DefaultZLinkNestFrameworkOptionsBuilder extends ZLinkNestOptionsBuilder {
  constructor() {
    super(createBuilderState());
  }
}

class DefaultZLinkNestCodecRegistryBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestCodecRegistryBuilder, ZLinkCodecRegistrar {
  constructor(state: ZLinkNestBuilderState) {
    super(state);
  }

  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this {
    super.addSerializer(contentType, serializer);
    return this;
  }

  addStreamCodec(contentType: string, codec: unknown): this {
    if (contentType.trim().length === 0) {
      throw new Error('Codec content type must not be empty.');
    }
    super.addStreamCodec(contentType, codec);
    return this;
  }

  use(extension: ZLinkCodecExtension): this {
    extension.register(this);
    return this;
  }

}

class DefaultZLinkNestClientServerChannelBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestClientServerChannelBuilder {
  constructor(state: ZLinkNestBuilderState, private readonly channelOptions: Mutable<InternalZLinkNestClientServerChannelOptions>) {
    super(state);
  }

  enableServer(bind: string | undefined): this {
    this.channelOptions.server = { ...this.channelOptions.server, bind };
    return this;
  }

  routingId(routingId: string | undefined): this {
    this.channelOptions.server = {
      ...this.channelOptions.server,
      routingId
    };
    return this;
  }

  configureServerSocket(): ZLinkSocketConfig {
    this.channelOptions.server ??= {};
    return this.channelOptions.server;
  }

  configureClientSocket(): ZLinkSocketConfig {
    this.channelOptions.client ??= {};
    return this.channelOptions.client;
  }

  enableClient(endpoint?: string | readonly string[]): this {
    this.channelOptions.client = {
      ...this.channelOptions.client,
      ...(endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) })
    };
    return this;
  }

  addHandlerGroup(groupName: string): this {
    this.channelOptions.handlerGroups = [...(this.channelOptions.handlerGroups ?? []), groupName];
    return this;
  }

  addRequestHandler(packetName: string, handlerType: Type): this {
    this.channelOptions.requestHandlerTypes = [...(this.channelOptions.requestHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }

  addSendHandler(packetName: string, handlerType: Type): this {
    this.channelOptions.sendHandlerTypes = [...(this.channelOptions.sendHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }
}

class DefaultZLinkNestFanoutChannelBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestFanoutChannelBuilder {
  constructor(state: ZLinkNestBuilderState, private readonly channelOptions: Mutable<InternalZLinkNestFanoutChannelOptions>) {
    super(state);
  }

  enablePublisher(bind: string | undefined): this {
    this.channelOptions.publisher = { bind };
    return this;
  }

  enableSubscriber(endpoint?: string | readonly string[]): this {
    this.channelOptions.subscriber = endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) };
    return this;
  }

  addHandlerGroup(groupName: string): this {
    this.channelOptions.handlerGroups = [...(this.channelOptions.handlerGroups ?? []), groupName];
    return this;
  }

  addPublishHandler(packetName: string, handlerType: Type): this {
    this.channelOptions.publishHandlerTypes = [...(this.channelOptions.publishHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }
}

class DefaultZLinkNestRouterMeshBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestRouterMeshBuilder {
  constructor(state: ZLinkNestBuilderState, private readonly routeOptions: Mutable<InternalZLinkNestRouterMeshOptions>) {
    super(state);
  }

  enableRouter(endpoint: string | undefined): this {
    this.routeOptions.bind = endpoint;
    return this;
  }

  enableClient(): this {
    markRouteClientEnabled(this.routeOptions);
    this.routeOptions.manualConnections = [];
    return this;
  }

  routingId(routingId: string | undefined): this {
    this.routeOptions.routingId = routingId;
    return this;
  }

  configureSocket(): ZLinkSocketConfig {
    return this.routeOptions;
  }

  connect(endpoint: string | readonly string[] | undefined): this {
    markRouteClientEnabled(this.routeOptions);
    this.routeOptions.manualConnections = endpoint === undefined ? [] : endpointList(endpoint);
    return this;
  }

  addHandlerGroup(groupName: string): this {
    this.routeOptions.handlerGroups = [...(this.routeOptions.handlerGroups ?? []), groupName];
    return this;
  }

  addSendHandler(packetName: string, handlerType: Type): this {
    this.routeOptions.sendHandlerTypes = [...(this.routeOptions.sendHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }

  addRequestHandler(packetName: string, handlerType: Type): this {
    this.routeOptions.requestHandlerTypes = [...(this.routeOptions.requestHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }
}

class DefaultZLinkNestStreamNodeBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestStreamNodeBuilder {
  constructor(state: ZLinkNestBuilderState, private readonly streamOptions: Mutable<ZLinkStreamNodeOptions>) {
    super(state);
  }

  bind(endpoint: string | undefined): this {
    this.streamOptions.bind = endpoint;
    return this;
  }

  setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate: boolean = false): this {
    this.streamOptions.tlsServer = {
      certificatePath,
      keyPath,
      requireClientCertificate
    };
    return this;
  }

  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this {
    if (this.streamOptions.session !== undefined) {
      throw new framework.ZLinkConfigurationException('STREAM node cannot register more than one header stream session.');
    }
    this.streamOptions.session = sessionType as Type;
    return this;
  }

}

class DefaultZLinkNestSpotNodeBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestSpotNodeBuilder {
  constructor(state: ZLinkNestBuilderState, private readonly spotOptions: Mutable<ZLinkSpotNodeOptions>) {
    super(state);
  }

  routingId(routingId: string | undefined): this {
    this.spotOptions.routingId = routingId;
    if (this.spotOptions.router !== undefined) {
      (this.spotOptions.router as Mutable<NonNullable<ZLinkSpotNodeOptions['router']>>).routingId = routingId;
    }
    if (this.spotOptions.pubSub !== undefined) {
      (this.spotOptions.pubSub as Mutable<NonNullable<ZLinkSpotNodeOptions['pubSub']>>).routingId = routingId;
    }
    return this;
  }

  enableRouter(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this {
    this.spotOptions.router = {
      ...(this.spotOptions.router ?? {}),
      bind,
      routingId: routingId ?? this.spotOptions.routingId,
      manualConnections: connect === undefined ? this.spotOptions.router?.manualConnections : endpointList(connect)
    };
    return this;
  }

  connectRouter(peerRidOrEndpoint: string, endpoint?: string): this {
    this.spotOptions.router ??= {};
    const router = this.spotOptions.router as {
      manualConnections?: string[];
      manualPeerConnections?: { peerRid: string; endpoint: string }[];
    };
    if (endpoint === undefined) {
      router.manualConnections ??= [];
      router.manualConnections.push(peerRidOrEndpoint);
      return this;
    }
    router.manualPeerConnections ??= [];
    router.manualPeerConnections.push({
      peerRid: peerRidOrEndpoint,
      endpoint
    });
    return this;
  }

  enablePubSub(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this {
    this.spotOptions.pubSub = {
      ...(this.spotOptions.pubSub ?? {}),
      bind,
      routingId: routingId ?? this.spotOptions.routingId,
      manualConnections: connect === undefined ? this.spotOptions.pubSub?.manualConnections : endpointList(connect)
    };
    return this;
  }

  connectPeerPub(endpoint: string): this {
    this.spotOptions.pubSub ??= {};
    const pubSub = this.spotOptions.pubSub as {
      manualConnections?: string[];
    };
    pubSub.manualConnections ??= [];
    pubSub.manualConnections.push(endpoint);
    return this;
  }

  configureEntrySpot(options: ZLinkEntrySpotOptions): this {
    this.spotOptions.entrySpot = { ...options };
    return this;
  }

  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this {
    framework.registerEntrySpot(this.spotOptions, entrySpotType);
    return this;
  }

  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this {
    const spotFactories = [...(this.spotOptions.spotFactories ?? [])];
    framework.registerSpotFactory({ spotFactories }, spotType);
    this.spotOptions.spotFactories = spotFactories;
    return this;
  }

  actorFactory(actorType: string, factoryType: Type): this {
    const actorFactories = { ...(this.spotOptions.actorFactories as Record<string, Type> | undefined) };
    framework.registerActorFactory({ actorFactories }, actorType, factoryType);
    this.spotOptions.actorFactories = actorFactories;
    return this;
  }

  useDrainPolicy(policy: import('@zlink-systems/framework').ZLinkSpotDrainPolicy): this {
    this.spotOptions.drainPolicy = policy;
    return this;
  }
}

function endpointList(endpoint: string | readonly string[]): string[] {
  return typeof endpoint === 'string' ? [endpoint] : [...endpoint];
}

function markRouteClientEnabled(routeOptions: object): void {
  defineRouteInternalFlag(routeOptions, 'clientEnabled');
}

function markRouteTransportDeclared(routeOptions: object): void {
  defineRouteInternalFlag(routeOptions, 'transportDeclared');
}

export function copyRouteInternalState(source: object, target: object): void {
  const state = source as InternalZLinkNestRouterMeshOptions;
  if (state.clientEnabled === true) {
    markRouteClientEnabled(target);
  }
  if (state.transportDeclared === true) {
    markRouteTransportDeclared(target);
  }
}

function defineRouteInternalFlag(routeOptions: object, key: 'clientEnabled' | 'transportDeclared'): void {
  Object.defineProperty(routeOptions, key, {
    value: true,
    configurable: true,
    enumerable: false,
    writable: true
  });
}

export function createZLinkNestFrameworkOptionsBuilder(): ZLinkNestFrameworkOptionsBuilder {
  return new DefaultZLinkNestFrameworkOptionsBuilder();
}
