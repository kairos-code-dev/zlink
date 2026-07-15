import type { InjectionToken, ModuleMetadata } from '@nestjs/common';
import type {
  Type,
  ZLinkActor,
  ZLinkActorTransferAdapter,
  ZLinkCodecExtension,
  ZLinkDispatchOptionsBuilder,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkLocationStore,
  ZLinkLocationOptions,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkSendContext,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkSocketConfig,
  ZLinkSpot,
  ZLinkStreamCompressionBuilder,
  ZLinkTimerOptions
} from '@zlink-systems/framework';
import type {
  ZLinkChannelPublishHandlerRegistration,
  ZLinkChannelRequestHandlerRegistration,
  ZLinkChannelSendHandlerRegistration,
  ZLinkClientCapabilityOptions,
  ZLinkCodecRegistryOptions,
  ZLinkFrameworkRegistrationOptions,
  ZLinkPublisherCapabilityOptions,
  ZLinkRouteChannelOptions,
  ZLinkRouteChannelRequestHandlerRegistration,
  ZLinkRouteChannelSendHandlerRegistration,
  ZLinkSpotNodeOptions,
  ZLinkSpotNodeRegistrationOptions,
  ZLinkStreamNodeOptions
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

export interface InternalZLinkNestClientServerChannelOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly routingId?: string;
  readonly routingIdAllocation?: ZLinkRoutingIdAllocationOptions;
  readonly server?: {
    readonly bind?: string;
    readonly routingId?: string;
    readonly weight?: number;
  };
  readonly client?: ZLinkClientCapabilityOptions;
  readonly requestHandlers?: readonly ZLinkChannelRequestHandlerRegistration[];
  readonly requestHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
  readonly sendHandlers?: readonly ZLinkChannelSendHandlerRegistration[];
  readonly sendHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
}

export interface InternalZLinkNestFanoutChannelOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly routingId?: string;
  readonly routingIdAllocation?: ZLinkRoutingIdAllocationOptions;
  readonly publisher?: ZLinkPublisherCapabilityOptions;
  readonly subscriber?: ZLinkClientCapabilityOptions;
  readonly publishHandlers?: readonly ZLinkChannelPublishHandlerRegistration[];
  readonly publishHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
}

export interface InternalZLinkNestRouterMeshOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly bind?: string;
  readonly clientEnabled?: boolean;
  readonly transportDeclared?: boolean;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly routingIdAllocation?: ZLinkRoutingIdAllocationOptions;
  readonly weight?: number;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly sendHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
  readonly requestHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
  readonly handlers?: ZLinkRouteChannelOptions['handlers'];
}

export interface ZLinkNestHandlerOptions {
  readonly methodName?: string;
  readonly decodePayload?: (
    payload: Buffer,
    context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
  ) => unknown;
  readonly encodeResult?: (
    result: unknown,
    context: ZLinkRequestContext | ZLinkRouteRequestContext
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
  readonly topic: string;
}

export interface ZLinkNestEntrySpotSubscriptionHandlerOptions<TEntrySpot extends ZLinkEntrySpot> {
  readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
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
  readonly routerMeshes?: Readonly<Record<string, InternalZLinkNestRouterMeshOptions>>;
  readonly spotNodes?: readonly (string | ZLinkSpotNodeRegistrationOptions)[] |
    Readonly<Record<string, ZLinkSpotNodeOptions>>;
  readonly streams?: Readonly<Record<string, ZLinkStreamNodeOptions>>;
}

export interface ZLinkNestFrameworkOptionsBuilder {
  options(options: ZLinkNestFrameworkAdditionalOptions): this;
  codecs(): ZLinkNestCodecRegistryBuilder;
  configureDispatch(): ZLinkDispatchOptionsBuilder;
  useInMemoryLocationStores(): this;
  addLocationStore(store: ZLinkLocationStore): this;
  addActorTransferAdapter<TActor extends ZLinkActor>(
    actorType: Type<TActor>,
    adapterType: Type<ZLinkActorTransferAdapter<TActor>>
  ): this;
  setActorTransferForwardWindow(timeoutMs: number): this;
  configureStreamCompression(): ZLinkStreamCompressionBuilder;
  configureLocations(): ZLinkLocationOptions;
  addClientServerChannel(name: string): ZLinkNestClientServerChannelBuilder;
  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder;
  addRouteMeshChannel(name: string): ZLinkNestRouterMeshBuilder;
  addSpotMesh(name: string): ZLinkNestSpotNodeBuilder;
  addStreamNode(name: string): ZLinkNestStreamNodeBuilder;
  build(): ZLinkModuleOptions;
}

export type ZLinkNestFrameworkAdditionalOptions = Omit<
  ZLinkFrameworkRegistrationOptions,
  'channels' | 'routeChannels' | 'streamNodes' | 'spotNodes' | 'codecs'
>;

export interface ZLinkNestCodecRegistryBuilder extends ZLinkNestFrameworkOptionsBuilder {
  use(extension: ZLinkCodecExtension): this;
}

export interface ZLinkNestClientServerChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
  enableServer(bind: string | undefined): this;
  routingId(routingId: string | undefined): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  configureServerSocket(): ZLinkSocketConfig;
  configureClientSocket(): ZLinkSocketConfig;
  enableClient(endpoint?: string | readonly string[]): this;
  addRequestHandler(packetName: string, handlerType: Type): this;
  addSendHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestFanoutChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
  enablePublisher(bind: string | undefined): this;
  routingId(routingId: string | undefined): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  enableSubscriber(endpoint?: string | readonly string[]): this;
  addPublishHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestRouterMeshBuilder extends ZLinkNestFrameworkOptionsBuilder {
  enableRouter(endpoint: string | undefined): this;
  enableClient(): this;
  routingId(routingId: string | undefined): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  configureSocket(): ZLinkSocketConfig;
  connect(endpoint: string | readonly string[] | undefined): this;
  addSendHandler(packetName: string, handlerType: Type): this;
  addRequestHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestStreamNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
  bind(endpoint: string | undefined): this;
  setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}

export interface ZLinkNestSpotNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
  routingId(routingId: string | undefined): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  enableRouter(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this;
  connectRouter(endpoint: string): this;
  connectRouter(peerRid: string, endpoint: string): this;
  enablePubSub(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this;
  connectPeerPub(endpoint: string): this;
  configureEntrySpot(options: ZLinkEntrySpotOptions): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  actorFactory(actorType: string, factoryType: Type): this;
  useDrainPolicy(policy: import('@zlink-systems/framework').ZLinkSpotDrainPolicy): this;
}

interface ZLinkRoutingIdAllocationOptions {
  readonly slotCount: number;
  readonly routingIdPrefix: string;
  readonly groupName?: string;
}
