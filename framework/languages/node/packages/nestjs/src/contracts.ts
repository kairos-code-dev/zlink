import type { InjectionToken, ModuleMetadata } from '@nestjs/common';
import type {
  Type,
  ZLinkActor,
  ZLinkActorFactory,
  ZLinkCodecExtension,
  ZLinkDispatchOptions,
  ZLinkDispatchOptionsBuilder,
  ZLinkEntrySpot,
  ZLinkHandlerFilter,
  ZLinkLocationStore,
  ZLinkLocationOptions,
  ZLinkRelocationStore,
  ZLinkActorFactoryBuilder,
  ZLinkInstanceSpotFactoryBuilder,
  ZLinkUserSpotFactoryBuilder,
  ZLinkInstanceSpot,
  ZLinkMeshNodeSocketConfig,
  ZLinkMeshPeerConnections,
  ZLinkMetricsOptions,
  ZLinkMonitoringOptions,
  ZLinkMessageContext,
  ZLinkPublishMessageContext,
  ZLinkRouteMessageContext,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkSpot,
  ZLinkSpotPublisherConfig,
  ZLinkStreamCompressionBuilder,
  ZLinkTimerOptions
} from '@zlink-systems/framework';
import type {
  ZLinkChannelOptions,
  ZLinkChannelPublishHandlerRegistration,
  ZLinkClientCapabilityOptions,
  ZLinkCodecRegistryOptions,
  ZLinkFrameworkRegistrationOptions,
  ZLinkPublisherCapabilityOptions,
  ZLinkSpotNodeOptions,
  ZLinkSpotNodeRegistrationOptions,
  ZLinkStreamNodeOptions,
  ZLinkWorkerOptions
} from './framework-integration-contracts';

export type MutableCodecRegistryOptions = {
  serializers: NonNullable<ZLinkCodecRegistryOptions['serializers']>[number][];
  streamCodecs: NonNullable<ZLinkCodecRegistryOptions['streamCodecs']>[number][];
};

export interface ZLinkModuleFactoryOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkModuleOptions | Promise<ZLinkModuleOptions>;
  readonly inject?: readonly InjectionToken[];
  readonly imports?: ModuleMetadata['imports'];
}

export interface ZLinkNestHandlerDiscoveryOptions {
  readonly handlerGroups?: readonly string[];
}

export interface ZLinkNestManualHandlerOptions {
  readonly packetName: string;
  readonly handlerType: Type;
}

export type Mutable<T> = {
  -readonly [K in keyof T]: T[K];
};

export interface InternalZLinkNestFanoutChannelOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly routingId?: string;
  readonly routingIdPrefix?: string;
  readonly publisher?: ZLinkPublisherCapabilityOptions;
  readonly subscriber?: ZLinkClientCapabilityOptions;
  readonly publishHandlers?: readonly ZLinkChannelPublishHandlerRegistration[];
  readonly publishHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
}

export interface InternalZLinkNestClientServerChannelOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly client?: ZLinkClientCapabilityOptions;
  readonly server?: NonNullable<ZLinkChannelOptions['server']>;
  readonly requestHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
  readonly sendHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
}

export interface ZLinkNestHandlerOptions {
  readonly methodName?: string;
  readonly decodePayload?: (
    payload: Buffer,
    context: ZLinkMessageContext | ZLinkRouteMessageContext | ZLinkPublishMessageContext
  ) => unknown;
  readonly encodeResult?: (
    result: unknown,
    context: ZLinkMessageContext | ZLinkRouteMessageContext
  ) => unknown;
}

export interface ZLinkNestProviderDiscoveryOptions {
  readonly recursive?: boolean;
}

export type ZLinkNestProviderDiscoveryRoot =
  | string
  | {
      readonly rootDir: string;
      readonly options?: ZLinkNestProviderDiscoveryOptions;
    };

export interface ZLinkNestModuleMetadata extends ModuleMetadata {
  readonly providerDiscovery?: readonly ZLinkNestProviderDiscoveryRoot[];
}

export type ZLinkNestModuleRoleRoot = string;

export type ZLinkNestTypeResolver<T> = Type<T> | (() => Type<T>);

export interface ZLinkNestSpotActorSendHandlerOptions<TSpot extends ZLinkSpot, TActor extends ZLinkActor> {
  readonly spot: ZLinkNestTypeResolver<TSpot>;
  readonly actor: ZLinkNestTypeResolver<TActor>;
  readonly packetName: string;
  readonly methodName?: string;
}

export interface ZLinkNestSpotPacketHandlerOptions<TSpot extends ZLinkSpot> {
  readonly spot: ZLinkNestTypeResolver<TSpot>;
  readonly packetName?: string;
}

export interface ZLinkNestEntrySpotPacketHandlerOptions<TEntrySpot extends ZLinkEntrySpot> {
  readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
  readonly packetName?: string;
}

export interface ZLinkNestSpotSubscriptionHandlerOptions<TSpot extends ZLinkSpot> {
  readonly spot: ZLinkNestTypeResolver<TSpot>;
  readonly channelName: string;
  readonly topic: string;
}

export interface ZLinkNestEntrySpotSubscriptionHandlerOptions<TEntrySpot extends ZLinkEntrySpot> {
  readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
  readonly channelName: string;
  readonly topic: string;
}

export interface ZLinkNestSpotActorRequestHandlerOptions<TSpot extends ZLinkSpot, TActor extends ZLinkActor> {
  readonly spot: ZLinkNestTypeResolver<TSpot>;
  readonly actor: ZLinkNestTypeResolver<TActor>;
  readonly packetName: string;
  readonly methodName?: string;
}

export interface ZLinkNestEntrySpotActorRequestHandlerOptions<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor> {
  readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
  readonly actor: ZLinkNestTypeResolver<TActor>;
  readonly packetName: string;
  readonly methodName?: string;
}

export interface ZLinkNestEntrySpotActorSendHandlerOptions<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor> {
  readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
  readonly actor: ZLinkNestTypeResolver<TActor>;
  readonly packetName: string;
  readonly methodName?: string;
}

export interface ZLinkNestSpotTimerHandlerOptions<TSpot extends ZLinkSpot = ZLinkSpot> {
  readonly spot?: ZLinkNestTypeResolver<TSpot>;
  readonly entrySpot?: ZLinkNestTypeResolver<ZLinkEntrySpot>;
  readonly name?: string;
  readonly periodMs?: number;
  readonly options?: ZLinkTimerOptions;
}

export const ZLINK_MODULE_OPTIONS_BRAND = Symbol('@zlink-systems/nestjs:module-options');

export interface ZLinkModuleOptions {
  readonly [ZLINK_MODULE_OPTIONS_BRAND]: true;
}

export interface ZLinkNestModuleRegistrationOptions extends Omit<
  ZLinkFrameworkRegistrationOptions,
  'channels' | 'routeChannels' | 'streamNodes' | 'spotNodes'
> {
  readonly [ZLINK_MODULE_OPTIONS_BRAND]: true;
  readonly clientServerChannels?: Readonly<Record<string, InternalZLinkNestClientServerChannelOptions>>;
  readonly fanoutChannels?: Readonly<Record<string, InternalZLinkNestFanoutChannelOptions>>;
  readonly spotNodes?: readonly (string | ZLinkSpotNodeRegistrationOptions)[] |
    Readonly<Record<string, ZLinkSpotNodeOptions>>;
  readonly streams?: Readonly<Record<string, ZLinkStreamNodeOptions>>;
}

export interface ZLinkNestFrameworkOptionsBuilder {
  options(options: ZLinkNestFrameworkAdditionalOptions): this;
  codecs(): ZLinkNestCodecRegistryBuilder;
  configureDispatch(): ZLinkDispatchOptionsBuilder;
  addLocationStore(store: ZLinkLocationStore): this;
  addRelocationStore(store: ZLinkRelocationStore): this;
  setApplicationVersion(version: bigint): this;
  setMaintenanceWave(waveId: string): this;
  setActorTransferTimeout(timeoutMs: number): this;
  setMessageFollowDuration(timeoutMs: number): this;
  configureStreamCompression(): ZLinkStreamCompressionBuilder;
  configureLocations(): ZLinkLocationOptions;
  addClientServerChannel(name: string): ZLinkNestClientServerChannelRoleBuilder;
  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder;
  addRouteMesh(name: string): ZLinkNestMeshNodeBuilder;
  addStreamNode(name: string): ZLinkNestStreamNodeBuilder;
  build(): ZLinkModuleOptions;
}

export interface ZLinkNestFrameworkAdditionalOptions {
  readonly requestTimeoutMs?: number;
  readonly filters?: readonly Type<ZLinkHandlerFilter>[];
  readonly worker?: ZLinkWorkerOptions;
  readonly dispatch?: ZLinkDispatchOptions;
  readonly monitoring?: ZLinkMonitoringOptions;
  readonly metrics?: ZLinkMetricsOptions;
}

export interface ZLinkNestCodecRegistryBuilder extends ZLinkNestFrameworkOptionsBuilder {
  use(extension: ZLinkCodecExtension): this;
}

export interface ZLinkNestFanoutChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
  enablePublisher(bind: string | undefined): this;
  routingId(routingId: string | undefined): this;
  setRoutingIdPrefix(prefix: string): this;
  enableSubscriber(endpoint?: string | readonly string[]): this;
  addPublishHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestClientServerChannelRoleBuilder extends ZLinkNestFrameworkOptionsBuilder {
  client(): ZLinkNestClientServerChannelClientBuilder;
  server(): ZLinkNestClientServerChannelServerBuilder;
}

export interface ZLinkNestClientServerChannelClientBuilder extends ZLinkNestFrameworkOptionsBuilder {
  connect(endpoint: string): this;
}

export interface ZLinkNestClientServerChannelServerBuilder extends ZLinkNestFrameworkOptionsBuilder {
  listen(port?: number): this;
  setBindHost(bindHost: string): this;
  setAdvertiseHost(advertiseHost: string): this;
  setWeight(weight: number): this;
  addSendHandler(packetName: string, handlerType: Type): this;
  addRequestHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestStreamNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
  bind(endpoint: string | undefined): this;
  enableActorDispatch(meshName: string): this;
  setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}

export interface ZLinkNestMeshNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
  channel(name: string): ZLinkNestMeshChannelBuilder;
  listen(endpoint: string): this;
  routingId(routingId: string | undefined): this;
  setRoutingIdPrefix(prefix: string): this;
  configureRouterSocket(): ZLinkMeshNodeSocketConfig;
  configureSpotPublisher(): ZLinkSpotPublisherConfig;
  peerConnections(): ZLinkMeshPeerConnections;
  objects(): ZLinkNestMeshObjectRoleBuilder;
  addSendHandler(packetName: string, handlerType: Type): this;
  addRequestHandler(packetName: string, handlerType: Type): this;
}

export interface ZLinkNestMeshObjectRoleBuilder extends ZLinkNestFrameworkOptionsBuilder {
  client(): ZLinkNestMeshObjectClientBuilder;
  server(): ZLinkNestMeshObjectServerBuilder;
}

export interface ZLinkNestMeshObjectClientBuilder extends ZLinkNestFrameworkOptionsBuilder {}

export interface ZLinkNestMeshObjectServerBuilder extends ZLinkNestFrameworkOptionsBuilder {
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(
    spotType: string,
    implementation: Type<TSpot>,
    configure: (builder: ZLinkUserSpotFactoryBuilder<TSpot>) => void
  ): this;
  addInstanceSpotFactory<TSpot extends ZLinkInstanceSpot>(
    instanceSpotType: string,
    implementation: Type<TSpot>,
    configure: (builder: ZLinkInstanceSpotFactoryBuilder<TSpot>) => void
  ): this;
  addActorFactory<TActor extends ZLinkActor>(
    actorType: string,
    implementation: Type<ZLinkActorFactory<TActor>>,
    configure: (builder: ZLinkActorFactoryBuilder<TActor>) => void
  ): this;
}

export interface ZLinkNestMeshChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
  client(): ZLinkNestMeshChannelClientBuilder;
  server(): ZLinkNestMeshChannelServerBuilder;
}

export interface ZLinkNestMeshChannelClientBuilder extends ZLinkNestFrameworkOptionsBuilder {}

export interface ZLinkNestMeshChannelServerBuilder extends ZLinkNestFrameworkOptionsBuilder {
  setWeight(weight: number): this;
  addSendHandler(packetName: string, handlerType: Type): this;
  addRequestHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}
