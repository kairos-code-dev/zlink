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
  IZLinkLocationStore,
  ZLinkLocationOptions,
  ZLinkMessageSerializer,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkSocketConfig,
  ZLinkSpot,
  ZLinkSpotNodeOptions,
  ZLinkStreamCompressionBuilder,
  ZLinkStreamNodeOptions
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


class DefaultZLinkNestFrameworkOptionsBuilder implements ZLinkNestFrameworkOptionsBuilder {
  private additionalOptions: ZLinkNestFrameworkAdditionalOptions = {};
  private readonly clientServerChannels: Record<string, InternalZLinkNestClientServerChannelOptions> = {};
  private readonly fanoutChannels: Record<string, InternalZLinkNestFanoutChannelOptions> = {};
  private readonly routerMeshes: Record<string, InternalZLinkNestRouterMeshOptions> = {};
  private readonly streams: Record<string, ZLinkStreamNodeOptions> = {};
  private readonly spotNodes: Record<string, ZLinkSpotNodeOptions> = {};
  private readonly codecOptions: MutableCodecRegistryOptions = { serializers: [], streamCodecs: [] };
  private readonly codecRegistry = framework.createIntegrationCodecRegistryBuilder(this.codecOptions);

  options(options: ZLinkNestFrameworkAdditionalOptions): this {
    this.additionalOptions = { ...this.additionalOptions, ...options };
    return this;
  }

  codecs(): ZLinkNestCodecRegistryBuilder {
    return new DefaultZLinkNestCodecRegistryBuilder(this);
  }

  configureDispatch(): ZLinkDispatchOptionsBuilder {
    this.additionalOptions = {
      ...this.additionalOptions,
      dispatch: this.additionalOptions.dispatch ?? {}
    };
    return framework.createIntegrationDispatchOptionsBuilder(
      this.additionalOptions.dispatch as NonNullable<ZLinkFrameworkRegistrationOptions['dispatch']>
    );
  }

  useInMemoryLocationStores(): this {
    this.additionalOptions = {
      ...this.additionalOptions,
      locations: {
        ...(this.additionalOptions.locations ?? {}),
        useInMemoryStores: true
      }
    };
    return this;
  }

  addLocationStore(store: IZLinkLocationStore): this {
    this.additionalOptions = {
      ...this.additionalOptions,
      locations: {
        ...(this.additionalOptions.locations ?? {}),
        storeInstance: store
      }
    };
    return this;
  }

  addActorTransferAdapter<TActor extends ZLinkActor>(
    actorType: Type<TActor>,
    adapterType: Type<ZLinkActorTransferAdapter<TActor>>
  ): this {
    const adapters = new Map(this.additionalOptions.actorTransferAdapters);
    framework.registerActorTransferAdapter(adapters, actorType, adapterType);
    this.additionalOptions = {
      ...this.additionalOptions,
      actorTransferAdapters: adapters
    };
    return this;
  }

  setActorTransferForwardWindow(timeoutMs: number): this {
    this.additionalOptions = {
      ...this.additionalOptions,
      actorTransferForwardWindowMs: framework.validateActorTransferForwardWindow(timeoutMs)
    };
    return this;
  }

  configureStreamCompression(): ZLinkStreamCompressionBuilder {
    const compression = { ...(this.additionalOptions.streamCompression ?? {}) };
    this.additionalOptions = { ...this.additionalOptions, streamCompression: compression };
    return framework.createIntegrationStreamCompressionBuilder(compression);
  }

  configureLocations(): ZLinkLocationOptions {
    this.additionalOptions = {
      ...this.additionalOptions,
      locations: this.additionalOptions.locations ?? { options: {} }
    };
    const locations = this.additionalOptions.locations as NonNullable<ZLinkFrameworkRegistrationOptions['locations']>;
    (locations as { options?: ZLinkLocationOptions }).options ??= {};
    return locations.options as ZLinkLocationOptions;
  }

  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): void {
    this.codecRegistry.addSerializer(contentType, serializer);
  }

  addStreamCodec(contentType: string, codec: unknown): void {
    this.codecRegistry.addStreamCodec(contentType, codec);
  }

  addClientServerChannel(name: string): ZLinkNestClientServerChannelBuilder {
    this.clientServerChannels[name] ??= {};
    return new DefaultZLinkNestClientServerChannelBuilder(this, this.clientServerChannels[name]);
  }

  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder {
    this.fanoutChannels[name] ??= {};
    return new DefaultZLinkNestFanoutChannelBuilder(this, this.fanoutChannels[name]);
  }

  addRouteMeshChannel(name: string): ZLinkNestRouterMeshBuilder {
    this.routerMeshes[name] ??= {};
    markRouteTransportDeclared(this.routerMeshes[name]);
    return new DefaultZLinkNestRouterMeshBuilder(this, this.routerMeshes[name]);
  }

  addSpotMesh(name: string): ZLinkNestSpotNodeBuilder {
    this.spotNodes[name] ??= {};
    return new DefaultZLinkNestSpotNodeBuilder(this, this.spotNodes[name]);
  }

  addStreamNode(name: string): ZLinkNestStreamNodeBuilder {
    this.streams[name] ??= {};
    return new DefaultZLinkNestStreamNodeBuilder(this, this.streams[name]);
  }

  build(): ZLinkModuleOptions {
    const options: ZLinkNestModuleRegistrationOptions = {
      [ZLINK_MODULE_OPTIONS_BRAND]: true,
      ...this.additionalOptions,
      clientServerChannels: { ...this.clientServerChannels },
      fanoutChannels: { ...this.fanoutChannels },
      routerMeshes: { ...this.routerMeshes },
      streams: { ...this.streams },
      spotNodes: { ...this.spotNodes },
      codecs: this.codecOptions.serializers.length === 0 &&
          this.codecOptions.streamCodecs.length === 0
        ? undefined
        : {
            serializers: [...this.codecOptions.serializers],
            streamCodecs: [...this.codecOptions.streamCodecs]
          }
    };
    return options;
  }
}

abstract class ZLinkNestChildBuilder implements ZLinkNestFrameworkOptionsBuilder {
  protected constructor(protected readonly root: DefaultZLinkNestFrameworkOptionsBuilder) {}

  options(options: ZLinkNestFrameworkAdditionalOptions): this {
    this.root.options(options);
    return this;
  }

  codecs(): ZLinkNestCodecRegistryBuilder {
    return this.root.codecs();
  }

  configureDispatch(): ZLinkDispatchOptionsBuilder {
    return this.root.configureDispatch();
  }

  useInMemoryLocationStores(): this {
    this.root.useInMemoryLocationStores();
    return this;
  }

  addLocationStore(store: IZLinkLocationStore): this {
    this.root.addLocationStore(store);
    return this;
  }

  addActorTransferAdapter<TActor extends ZLinkActor>(
    actorType: Type<TActor>,
    adapterType: Type<ZLinkActorTransferAdapter<TActor>>
  ): this {
    this.root.addActorTransferAdapter(actorType, adapterType);
    return this;
  }

  setActorTransferForwardWindow(timeoutMs: number): this {
    this.root.setActorTransferForwardWindow(timeoutMs);
    return this;
  }

  configureStreamCompression(): ZLinkStreamCompressionBuilder {
    return this.root.configureStreamCompression();
  }

  configureLocations(): ZLinkLocationOptions {
    return this.root.configureLocations();
  }

  addClientServerChannel(name: string): ZLinkNestClientServerChannelBuilder {
    return this.root.addClientServerChannel(name);
  }

  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder {
    return this.root.addFanoutChannel(name);
  }

  addRouteMeshChannel(name: string): ZLinkNestRouterMeshBuilder {
    return this.root.addRouteMeshChannel(name);
  }

  addSpotMesh(name: string): ZLinkNestSpotNodeBuilder {
    return this.root.addSpotMesh(name);
  }

  addStreamNode(name: string): ZLinkNestStreamNodeBuilder {
    return this.root.addStreamNode(name);
  }

  build(): ZLinkModuleOptions {
    return this.root.build();
  }
}

class DefaultZLinkNestCodecRegistryBuilder extends ZLinkNestChildBuilder implements ZLinkNestCodecRegistryBuilder, ZLinkCodecRegistrar {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder) {
    super(root);
  }

  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this {
    this.root.addSerializer(contentType, serializer);
    return this;
  }

  addStreamCodec(contentType: string, codec: unknown): this {
    if (contentType.trim().length === 0) {
      throw new Error('Codec content type must not be empty.');
    }
    this.root.addStreamCodec(contentType, codec);
    return this;
  }

  use(extension: ZLinkCodecExtension): this {
    extension.register(this);
    return this;
  }

}

class DefaultZLinkNestClientServerChannelBuilder extends ZLinkNestChildBuilder implements ZLinkNestClientServerChannelBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly channelOptions: Mutable<InternalZLinkNestClientServerChannelOptions>) {
    super(root);
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

class DefaultZLinkNestFanoutChannelBuilder extends ZLinkNestChildBuilder implements ZLinkNestFanoutChannelBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly channelOptions: Mutable<InternalZLinkNestFanoutChannelOptions>) {
    super(root);
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

class DefaultZLinkNestRouterMeshBuilder extends ZLinkNestChildBuilder implements ZLinkNestRouterMeshBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly routeOptions: Mutable<InternalZLinkNestRouterMeshOptions>) {
    super(root);
  }

  enableRouter(endpoint: string | undefined): this {
    this.routeOptions.bind = endpoint;
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

class DefaultZLinkNestStreamNodeBuilder extends ZLinkNestChildBuilder implements ZLinkNestStreamNodeBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly streamOptions: Mutable<ZLinkStreamNodeOptions>) {
    super(root);
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

class DefaultZLinkNestSpotNodeBuilder extends ZLinkNestChildBuilder implements ZLinkNestSpotNodeBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly spotOptions: Mutable<ZLinkSpotNodeOptions>) {
    super(root);
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
    this.spotOptions.entrySpotType = entrySpotType;
    return this;
  }

  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this {
    this.spotOptions.spotFactories = [...(this.spotOptions.spotFactories ?? []), spotType];
    return this;
  }

  actorFactory(actorType: string, factoryType: Type): this {
    this.spotOptions.actorFactories = {
      ...(this.spotOptions.actorFactories as Record<string, Type> | undefined),
      [actorType]: factoryType
    };
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
