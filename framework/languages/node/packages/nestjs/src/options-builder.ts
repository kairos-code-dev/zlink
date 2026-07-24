import type {
  Type,
  ZLinkActor,
  ZLinkActorFactory,
  ZLinkActorTransferAdapter,
  ZLinkCodecExtension,
  ZLinkCodecRegistrar,
  ZLinkDispatchOptionsBuilder,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkLocationStore,
  ZLinkLocationOptionValues,
  ZLinkLocationOptions,
  ZLinkRelocationStore,
  ZLinkRelocationPolicy,
  ZLinkActorFactoryOptions,
  ZLinkInstanceSpotFactoryOptions,
  ZLinkUserSpotFactoryOptions,
  ZLinkMeshNodeSocketConfig,
  ZLinkMeshPeerConnection,
  ZLinkMeshPeerConnections,
  ZLinkMessageSerializer,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkSpot,
  ZLinkInstanceSpot,
  ZLinkSpotPublisherConfig,
  ZLinkStreamCompressionBuilder,
} from '@zlink-systems/framework';
import type {
  ZLinkFrameworkRegistrationOptions,
  ZLinkSpotNodeOptions,
  ZLinkStreamNodeOptions
} from './framework-integration-contracts';
import {
  ZLinkMessageFlowLogMode,
  ZLinkUserSpotExecutionMode,
  ZLinkUnhandledDispatchAction
} from '@zlink-systems/framework';
import {
  ZLINK_MODULE_OPTIONS_BRAND,
  type InternalZLinkNestClientServerChannelOptions,
  type Mutable,
  type MutableCodecRegistryOptions,
  type ZLinkModuleOptions,
  type ZLinkNestClientServerChannelClientBuilder,
  type ZLinkNestClientServerChannelRoleBuilder,
  type ZLinkNestClientServerChannelServerBuilder,
  type ZLinkNestCodecRegistryBuilder,
  type ZLinkNestFanoutChannelBuilder,
  type InternalZLinkNestFanoutChannelOptions,
  type ZLinkNestFrameworkAdditionalOptions,
  type ZLinkNestFrameworkOptionsBuilder,
  type ZLinkNestModuleRegistrationOptions,
  type ZLinkNestMeshChannelBuilder,
  type ZLinkNestMeshObjectClientBuilder,
  type ZLinkNestMeshObjectRoleBuilder,
  type ZLinkNestMeshObjectServerBuilder,
  type ZLinkNestMeshNodeBuilder,
  type ZLinkNestStreamNodeBuilder
} from './contracts';
import { framework } from './framework-loader';


type ZLinkNestBuilderAdditionalOptions = Omit<
  ZLinkFrameworkRegistrationOptions,
  'channels' | 'routeChannels' | 'streamNodes' | 'spotNodes' | 'codecs'
>;

interface ZLinkNestBuilderState {
  additionalOptions: ZLinkNestBuilderAdditionalOptions;
  readonly clientServerChannels: Record<string, InternalZLinkNestClientServerChannelOptions>;
  readonly fanoutChannels: Record<string, InternalZLinkNestFanoutChannelOptions>;
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
          includeMessageSizes: false
        }
      }
    };
    return framework.createIntegrationDispatchOptionsBuilder(
      this.state.additionalOptions.dispatch as NonNullable<ZLinkFrameworkRegistrationOptions['dispatch']>
    );
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

  addRelocationStore(store: ZLinkRelocationStore): this {
    this.state.additionalOptions = {
      ...this.state.additionalOptions,
      locations: {
        ...(this.state.additionalOptions.locations ?? {}),
        relocationStoreInstance: store
      }
    };
    return this;
  }

  protected registerActorTransferAdapter<TActor extends ZLinkActor>(
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

  setActorTransferTimeout(timeoutMs: number): this {
    this.state.additionalOptions = {
      ...this.state.additionalOptions,
      actorTransferTimeoutMs: framework.validateActorTransferTimeout(timeoutMs)
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
    (locations as { options?: Partial<ZLinkLocationOptionValues> }).options ??= {};
    return framework.createIntegrationLocationOptionsBuilder(
      locations.options as Partial<ZLinkLocationOptionValues>
    );
  }

  protected addSerializer(contentType: string, serializer: ZLinkMessageSerializer): void {
    this.state.codecRegistry.addSerializer(contentType, serializer);
  }

  protected addStreamCodec(contentType: string, codec: unknown): void {
    this.state.codecRegistry.addStreamCodec(contentType, codec);
  }

  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder {
    this.ensureChannelAvailable(name);
    this.state.fanoutChannels[name] = {};
    return new DefaultZLinkNestFanoutChannelBuilder(this.state, name, this.state.fanoutChannels[name]);
  }

  addClientServerChannel(name: string): ZLinkNestClientServerChannelRoleBuilder {
    this.ensureClientServerChannelAvailable(name);
    this.state.clientServerChannels[name] ??= {};
    return new DefaultZLinkNestClientServerChannelRoleBuilder(
      this.state,
      name,
      this.state.clientServerChannels[name]
    );
  }

  addRouteMesh(name: string): ZLinkNestMeshNodeBuilder {
    if (name.trim().length === 0 || name.trim() !== name) {
      throw new framework.ZLinkConfigurationException('RouteMesh name must not be empty or padded.');
    }
    if (Object.prototype.hasOwnProperty.call(this.state.spotNodes, name)) {
      throw new framework.ZLinkConfigurationException(`Duplicate RouteMesh '${name}'.`);
    }
    this.state.spotNodes[name] = {};
    return new DefaultZLinkNestMeshNodeBuilder(this.state, name, this.state.spotNodes[name]);
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
      Object.prototype.hasOwnProperty.call(this.state.fanoutChannels, name)
      || Object.prototype.hasOwnProperty.call(this.state.clientServerChannels, name)
    ) {
      throw new framework.ZLinkConfigurationException(`Duplicate channel '${name}'.`);
    }
  }

  private ensureClientServerChannelAvailable(name: string): void {
    if (name.trim().length === 0 || name.trim() !== name) {
      throw new framework.ZLinkConfigurationException('Channel name must not be empty or padded.');
    }
    if (Object.prototype.hasOwnProperty.call(this.state.fanoutChannels, name)) {
      throw new framework.ZLinkConfigurationException(`Duplicate channel '${name}'.`);
    }
  }

  build(): ZLinkModuleOptions {
    const options: ZLinkNestModuleRegistrationOptions = {
      [ZLINK_MODULE_OPTIONS_BRAND]: true,
      ...this.state.additionalOptions,
      clientServerChannels: { ...this.state.clientServerChannels },
      fanoutChannels: { ...this.state.fanoutChannels },
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

class DefaultZLinkNestFanoutChannelBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestFanoutChannelBuilder {
  private subscriberMode?: 'automatic' | 'manual';

  constructor(state: ZLinkNestBuilderState, private readonly name: string, private readonly channelOptions: Mutable<InternalZLinkNestFanoutChannelOptions>) {
    super(state);
  }

  enablePublisher(bind: string | undefined): this {
    this.channelOptions.publisher = { bind };
    return this;
  }

  routingId(routingId: string | undefined): this {
    framework.rejectAllocatedRoutingId(this.channelOptions.routingIdAllocation, this.name);
    this.channelOptions.routingId = routingId;
    return this;
  }

  useAllocatedRoutingId(slotCount: number, routingIdPrefix = this.name): this {
    framework.rejectFixedRoutingId(this.channelOptions.routingId, this.name);
    this.channelOptions.routingIdAllocation = framework.createRoutingIdAllocation(slotCount, routingIdPrefix, this.channelOptions.routingIdAllocation?.groupName);
    return this;
  }

  setRoutingIdAllocationGroup(groupName: string): this {
    this.channelOptions.routingIdAllocation = framework.setRoutingIdAllocationGroup(groupName, this.channelOptions.routingIdAllocation, this.name);
    return this;
  }

  enableSubscriber(endpoint?: string | readonly string[]): this {
    const mode = endpoint === undefined ? 'automatic' : 'manual';
    if (this.subscriberMode !== undefined && this.subscriberMode !== mode) {
      throw new framework.ZLinkConfigurationException(
        `Fanout channel '${this.name}' cannot combine automatic and manual subscriber sources.`
      );
    }
    this.subscriberMode = mode;
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

class DefaultZLinkNestClientServerChannelRoleBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestClientServerChannelRoleBuilder {
  constructor(
    state: ZLinkNestBuilderState,
    private readonly name: string,
    private readonly channel: Mutable<InternalZLinkNestClientServerChannelOptions>
  ) {
    super(state);
  }

  client(): ZLinkNestClientServerChannelClientBuilder {
    if (this.channel.client !== undefined) {
      throw this.duplicate('Client');
    }
    this.channel.client = { manualConnections: [] };
    return new DefaultZLinkNestClientServerChannelClientBuilder(this.state, this.name, this.channel);
  }

  server(): ZLinkNestClientServerChannelServerBuilder {
    if (this.channel.server !== undefined) {
      throw this.duplicate('Server');
    }
    this.channel.server = {};
    return new DefaultZLinkNestClientServerChannelServerBuilder(this.state, this.name, this.channel);
  }

  private duplicate(role: 'Client' | 'Server') {
    return new framework.ZLinkConfigurationException(
      `ClientServer channel '${this.name}' ${role} role is already registered.`
    );
  }
}

class DefaultZLinkNestClientServerChannelClientBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestClientServerChannelClientBuilder {
  constructor(
    state: ZLinkNestBuilderState,
    private readonly name: string,
    private readonly channel: Mutable<InternalZLinkNestClientServerChannelOptions>
  ) {
    super(state);
  }

  connect(endpoint: string): this {
    requireClientServerText(endpoint, `ClientServer channel '${this.name}' endpoint`);
    const connections = [...(this.channel.client?.manualConnections ?? [])];
    if (!connections.includes(endpoint)) connections.push(endpoint);
    this.channel.client = { ...this.channel.client, manualConnections: connections };
    return this;
  }
}

class DefaultZLinkNestClientServerChannelServerBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestClientServerChannelServerBuilder {
  constructor(
    state: ZLinkNestBuilderState,
    private readonly name: string,
    private readonly channel: Mutable<InternalZLinkNestClientServerChannelOptions>
  ) {
    super(state);
  }

  listen(port = 0): this {
    if (!Number.isInteger(port) || port < 0 || port > 65_535) {
      throw new framework.ZLinkConfigurationException(
        `ClientServer channel '${this.name}' port must be between 0 and 65535.`
      );
    }
    this.updateServer({ port });
    this.updateBind();
    return this;
  }

  setBindHost(bindHost: string): this {
    requireClientServerText(bindHost, `ClientServer channel '${this.name}' bind host`);
    this.updateServer({ bindHost });
    if (this.server.port !== undefined) this.updateBind();
    return this;
  }

  setAdvertiseHost(advertiseHost: string): this {
    requireClientServerText(advertiseHost, `ClientServer channel '${this.name}' advertise host`);
    this.updateServer({ advertiseHost });
    return this;
  }

  setWeight(weight: number): this {
    if (!Number.isInteger(weight) || weight < 0 || weight > 100) {
      throw new framework.ZLinkConfigurationException(
        `ClientServer channel '${this.name}' weight must be between 0 and 100.`
      );
    }
    this.updateServer({ weight });
    return this;
  }

  addSendHandler(packetName: string, handlerType: Type): this {
    requireClientServerText(packetName, `ClientServer channel '${this.name}' send packet name`);
    this.channel.sendHandlerTypes = [
      ...(this.channel.sendHandlerTypes ?? []),
      { packetName, handlerType }
    ];
    return this;
  }

  addRequestHandler(packetName: string, handlerType: Type): this {
    requireClientServerText(packetName, `ClientServer channel '${this.name}' request packet name`);
    this.channel.requestHandlerTypes = [
      ...(this.channel.requestHandlerTypes ?? []),
      { packetName, handlerType }
    ];
    return this;
  }

  addHandlerGroup(groupName: string): this {
    requireClientServerText(groupName, `ClientServer channel '${this.name}' handler group`);
    this.channel.handlerGroups = [...(this.channel.handlerGroups ?? []), groupName];
    return this;
  }

  private get server(): NonNullable<InternalZLinkNestClientServerChannelOptions['server']> {
    return this.channel.server!;
  }

  private updateServer(
    values: Partial<NonNullable<InternalZLinkNestClientServerChannelOptions['server']>>
  ): void {
    this.channel.server = { ...this.channel.server, ...values };
  }

  private updateBind(): void {
    const host = this.server.bindHost ?? '127.0.0.1';
    const endpointHost = host.includes(':') && !host.startsWith('[') ? `[${host}]` : host;
    this.updateServer({ bind: `tcp://${endpointHost}:${this.server.port ?? 0}` });
  }
}

function requireClientServerText(value: string, label: string): void {
  if (value.trim().length === 0 || value.trim() !== value) {
    throw new framework.ZLinkConfigurationException(`${label} must not be empty or padded.`);
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

  enableActorDispatch(meshName: string): this {
    if (meshName.trim().length === 0 || meshName.trim() !== meshName) {
      throw new framework.ZLinkConfigurationException(
        'STREAM actor dispatch MeshName must not be empty or padded.'
      );
    }
    if (this.streamOptions.actorDispatchMeshName !== undefined) {
      throw new framework.ZLinkConfigurationException(
        'STREAM node actor dispatch MeshName is already configured.'
      );
    }
    this.streamOptions.actorDispatchMeshName = meshName;
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

class DefaultZLinkNestMeshNodeBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestMeshNodeBuilder {
  constructor(state: ZLinkNestBuilderState, private readonly name: string, private readonly spotOptions: Mutable<ZLinkSpotNodeOptions>) {
    super(state);
  }

  channelName(name: string): ZLinkNestMeshChannelBuilder {
    if (name.trim().length === 0 || name.trim() !== name) {
      throw new framework.ZLinkConfigurationException('Mesh channel name must not be empty or padded.');
    }
    const channels = {
      ...(this.spotOptions.meshChannels ?? {})
    } as Record<string, Mutable<NonNullable<ZLinkSpotNodeOptions['meshChannels']>[string]>>;
    if (Object.prototype.hasOwnProperty.call(channels, name)) {
      throw new framework.ZLinkConfigurationException(
        `Duplicate channel '${name}' in RouteMesh '${this.name}'.`
      );
    }
    channels[name] = {};
    this.spotOptions.meshChannels = channels;
    return new DefaultZLinkNestMeshChannelBuilder(this.state, channels[name]);
  }

  listen(endpoint: string): this {
    this.spotOptions.router = { ...(this.spotOptions.router ?? {}), bind: endpoint };
    return this;
  }

  routingId(routingId: string | undefined): this {
    framework.rejectAllocatedRoutingId(this.spotOptions.routingIdAllocation, this.name);
    this.spotOptions.routingId = routingId;
    if (this.spotOptions.router !== undefined) {
      (this.spotOptions.router as Mutable<NonNullable<ZLinkSpotNodeOptions['router']>>).routingId = routingId;
    }
    if (this.spotOptions.pubSub !== undefined) {
      (this.spotOptions.pubSub as Mutable<NonNullable<ZLinkSpotNodeOptions['pubSub']>>).routingId = routingId;
    }
    return this;
  }

  useAllocatedRoutingId(slotCount: number, routingIdPrefix = this.name): this {
    framework.rejectFixedRoutingId(this.spotOptions.routingId, this.name);
    this.spotOptions.routingIdAllocation = framework.createRoutingIdAllocation(slotCount, routingIdPrefix, this.spotOptions.routingIdAllocation?.groupName);
    return this;
  }

  setRoutingIdAllocationGroup(groupName: string): this {
    this.spotOptions.routingIdAllocation = framework.setRoutingIdAllocationGroup(groupName, this.spotOptions.routingIdAllocation, this.name);
    return this;
  }

  configureRouterSocket(): ZLinkMeshNodeSocketConfig {
    this.spotOptions.router ??= {};
    return this.spotOptions.router as ZLinkMeshNodeSocketConfig;
  }

  configureSpotPublisher(): ZLinkSpotPublisherConfig {
    this.spotOptions.publisherConfig ??= {};
    return this.spotOptions.publisherConfig as ZLinkSpotPublisherConfig;
  }

  peerConnections(): ZLinkMeshPeerConnections {
    this.spotOptions.router ??= {};
    return new DefaultZLinkNestMeshPeerConnections(this.spotOptions.router);
  }

  objects(): ZLinkNestMeshObjectRoleBuilder {
    return new DefaultZLinkNestMeshObjectRoleBuilder(
      this.state,
      this.name,
      this.spotOptions
    );
  }

  addSendHandler(packetName: string, handlerType: Type): this {
    this.spotOptions.routeSendHandlers = [
      ...(this.spotOptions.routeSendHandlers ?? []),
      { packetName, handlerType }
    ];
    return this;
  }

  addRequestHandler(packetName: string, handlerType: Type): this {
    this.spotOptions.routeRequestHandlers = [
      ...(this.spotOptions.routeRequestHandlers ?? []),
      { packetName, handlerType }
    ];
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

  addActorTransferAdapter<TActor extends ZLinkActor>(
    actorType: string,
    adapterType: Type<ZLinkActorTransferAdapter<TActor>>
  ): this {
    const actorFactory = this.spotOptions.actorFactories instanceof Map
      ? this.spotOptions.actorFactories.get(actorType)
      : (this.spotOptions.actorFactories as Readonly<Record<string, Type>> | undefined)?.[actorType];
    if (actorFactory === undefined) {
      throw new framework.ZLinkConfigurationException(
        `Actor transfer adapter '${actorType}' requires an actor factory on RouteMesh '${this.name}'.`
      );
    }
    this.registerActorTransferAdapter(actorFactory as Type<TActor>, adapterType);
    return this;
  }

}

class DefaultZLinkNestMeshObjectRoleBuilder
  extends ZLinkNestOptionsBuilder
  implements ZLinkNestMeshObjectRoleBuilder {
  constructor(
    state: ZLinkNestBuilderState,
    private readonly meshName: string,
    private readonly node: Mutable<ZLinkSpotNodeOptions>
  ) {
    super(state);
  }

  client(): ZLinkNestMeshObjectClientBuilder {
    this.node.objectRole = 'client';
    return new DefaultZLinkNestMeshObjectClientBuilder(this.state);
  }

  server(): ZLinkNestMeshObjectServerBuilder {
    this.node.objectRole = 'server';
    return new DefaultZLinkNestMeshObjectServerBuilder(
      this.state,
      this.meshName,
      this.node
    );
  }
}

class DefaultZLinkNestMeshObjectClientBuilder
  extends ZLinkNestOptionsBuilder
  implements ZLinkNestMeshObjectClientBuilder {}

class DefaultZLinkNestMeshObjectServerBuilder
  extends ZLinkNestOptionsBuilder
  implements ZLinkNestMeshObjectServerBuilder {
  constructor(
    state: ZLinkNestBuilderState,
    private readonly meshName: string,
    private readonly node: Mutable<ZLinkSpotNodeOptions>
  ) {
    super(state);
  }

  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(
    entrySpotType: Type<TEntrySpot>
  ): this {
    framework.registerEntrySpot(this.node, entrySpotType);
    return this;
  }

  addSpotFactory<TSpot extends ZLinkSpot>(
    spotType: string,
    implementation: Type<TSpot>,
    options: ZLinkUserSpotFactoryOptions | undefined,
    relocation: ZLinkRelocationPolicy<TSpot>
  ): this {
    const stableType = validateObjectFactory(
      spotType,
      'User Spot type',
      relocation
    );
    validateStableTypeLimit(options?.stableTypeLimit);
    if (
      options?.executionMode !== undefined
      && options.executionMode !== ZLinkUserSpotExecutionMode.SpotWide
      && options.executionMode !== ZLinkUserSpotExecutionMode.PerActor
    ) {
      throw new framework.ZLinkConfigurationException(
        'User Spot executionMode is invalid.'
      );
    }
    const registrations = {
      ...(this.node.spotFactoryRegistrations ?? {})
    };
    rejectDuplicateObjectType(registrations, stableType, this.meshName);
    registrations[stableType] = {
      implementation,
      options: {
        ...options,
        executionMode: options?.executionMode ?? ZLinkUserSpotExecutionMode.SpotWide
      },
      relocation
    };
    this.node.spotFactoryRegistrations = registrations;
    const factories = [...(this.node.spotFactories ?? [])];
    framework.registerSpotFactory({ spotFactories: factories }, implementation);
    this.node.spotFactories = factories;
    return this;
  }

  addInstanceSpotFactory<TSpot extends ZLinkInstanceSpot>(
    instanceSpotType: string,
    implementation: Type<TSpot>,
    options: ZLinkInstanceSpotFactoryOptions | undefined,
    relocation: ZLinkRelocationPolicy<TSpot>
  ): this {
    const stableType = validateObjectFactory(
      instanceSpotType,
      'Instance Spot type',
      relocation
    );
    validateStableTypeLimit(options?.stableTypeLimit);
    const factories = {
      ...(this.node.instanceSpotFactories ?? {})
    };
    if (Object.hasOwn(factories, stableType)) {
      throw new framework.ZLinkConfigurationException(
        `Duplicate Instance Spot factory '${stableType}' on RouteMesh '${this.meshName}'.`
      );
    }
    factories[stableType] = implementation;
    this.node.instanceSpotFactories = factories;
    this.node.instanceSpotFactoryRegistrations = {
      ...(this.node.instanceSpotFactoryRegistrations ?? {}),
      [stableType]: { implementation, options, relocation }
    };
    return this;
  }

  addActorFactory<TActor extends ZLinkActor>(
    actorType: string,
    implementation: Type<ZLinkActorFactory<TActor>>,
    options: ZLinkActorFactoryOptions | undefined,
    relocation: ZLinkRelocationPolicy<TActor>
  ): this {
    const stableType = validateObjectFactory(
      actorType,
      'Actor type',
      relocation
    );
    const registrations = {
      ...(this.node.actorFactoryRegistrations ?? {})
    };
    rejectDuplicateObjectType(registrations, stableType, this.meshName);
    const actorFactories = {
      ...(this.node.actorFactories as Readonly<Record<string, Type>> | undefined)
    };
    framework.registerActorFactory({ actorFactories }, stableType, implementation);
    this.node.actorFactories = actorFactories;
    registrations[stableType] = { implementation, options, relocation };
    this.node.actorFactoryRegistrations = registrations;
    return this;
  }
}

function validateObjectFactory<T>(
  stableType: string,
  label: string,
  relocation: ZLinkRelocationPolicy<T>
): string {
  if (
    typeof stableType !== 'string'
    || Buffer.byteLength(stableType) < 1
    || Buffer.byteLength(stableType) > 255
    || stableType.includes('\0')
  ) {
    throw new framework.ZLinkConfigurationException(
      `${label} must contain 1..255 UTF-8 bytes and no NUL.`
    );
  }
  if (relocation.kind === 'snapshot' && typeof relocation.adapterType !== 'function') {
    throw new framework.ZLinkConfigurationException(
      'Snapshot relocation requires an adapter type.'
    );
  }
  return stableType;
}

function validateStableTypeLimit(value: number | undefined): void {
  if (value !== undefined && (!Number.isSafeInteger(value) || value < 0)) {
    throw new framework.ZLinkConfigurationException(
      'stableTypeLimit must be a non-negative safe integer.'
    );
  }
}

function rejectDuplicateObjectType(
  registrations: Readonly<Record<string, unknown>>,
  stableType: string,
  meshName: string
): void {
  if (Object.hasOwn(registrations, stableType)) {
    throw new framework.ZLinkConfigurationException(
      `Duplicate object factory '${stableType}' on RouteMesh '${meshName}'.`
    );
  }
}

class DefaultZLinkNestMeshChannelBuilder extends ZLinkNestOptionsBuilder implements ZLinkNestMeshChannelBuilder {
  constructor(
    state: ZLinkNestBuilderState,
    private readonly channel: Mutable<NonNullable<ZLinkSpotNodeOptions['meshChannels']>[string]>
  ) {
    super(state);
  }

  setWeight(weight: number): this {
    this.channel.weight = weight;
    return this;
  }

  addSendHandler(packetName: string, handlerType: Type): this {
    this.channel.sendHandlers = [...(this.channel.sendHandlers ?? []), { packetName, handlerType }];
    return this;
  }

  addRequestHandler(packetName: string, handlerType: Type): this {
    this.channel.requestHandlers = [...(this.channel.requestHandlers ?? []), { packetName, handlerType }];
    return this;
  }

  addHandlerGroup(groupName: string): this {
    this.channel.handlerGroups = [...(this.channel.handlerGroups ?? []), groupName];
    return this;
  }
}

class DefaultZLinkNestMeshPeerConnections implements ZLinkMeshPeerConnections {
  constructor(private readonly router: Mutable<NonNullable<ZLinkSpotNodeOptions['router']>>) {}

  connect(endpoint: string): void;
  connect(expectedRoutingId: string, endpoint: string): void;
  connect(expectedRoutingIdOrEndpoint: string, endpoint?: string): void {
    if (endpoint === undefined) {
      this.router.manualConnections = [...(this.router.manualConnections ?? []), expectedRoutingIdOrEndpoint];
      return;
    }
    this.router.manualPeerConnections = [
      ...(this.router.manualPeerConnections ?? []),
      { peerRid: expectedRoutingIdOrEndpoint, endpoint }
    ];
  }

  disconnect(endpoint: string): void {
    this.router.manualConnections = (this.router.manualConnections ?? [])
      .filter((value) => value !== endpoint);
    this.router.manualPeerConnections = (this.router.manualPeerConnections ?? [])
      .filter((value) => value.endpoint !== endpoint);
  }

  listConnections(): readonly ZLinkMeshPeerConnection[] {
    return [
      ...(this.router.manualConnections ?? []).map((endpoint) => ({ endpoint })),
      ...(this.router.manualPeerConnections ?? []).map((connection) => ({
        endpoint: connection.endpoint,
        expectedRoutingId: connection.peerRid
      }))
    ];
  }
}

function endpointList(endpoint: string | readonly string[]): string[] {
  return typeof endpoint === 'string' ? [endpoint] : [...endpoint];
}

export function createZLinkNestFrameworkOptionsBuilder(): ZLinkNestFrameworkOptionsBuilder {
  return new DefaultZLinkNestFrameworkOptionsBuilder();
}
