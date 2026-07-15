import type {
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkDispatchOptions,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkHandlerFilter,
  ZLinkLocationOptions,
  ZLinkLocationStore,
  ZLinkMessageSerializer,
  ZLinkMetricsOptions,
  ZLinkMonitoringOptions,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkRuntimeEventPublisher,
  ZLinkSendContext,
  ZLinkSpot,
  ZLinkSpotDrainPolicy,
  ZLinkStreamCompressionOptions,
  ZLinkTimerOptions
} from '@zlink-systems/framework';

export interface ZLinkWorkerOptions {
  readonly maxThreads?: number;
  readonly maxQueueLength?: number;
}

export interface ZLinkNestIntegrationRuntimeHost {
  readonly channelRuntimeOptions: unknown;
  readonly boundSessionFactory: unknown;
  readonly eventPublisher: ZLinkRuntimeEventPublisher;
  readonly locationRuntimeQuery?: unknown;
  createLocationHandleResolver(): unknown;
  start(): Promise<void>;
  stop(): Promise<void>;
  drain(deadlineMs?: number, signal?: AbortSignal): Promise<unknown>;
  awaitDrained(signal?: AbortSignal): Promise<unknown>;
  isReady(): boolean;
  onApplicationBootstrap?(): Promise<void> | void;
  onApplicationShutdown?(): Promise<void> | void;
}

export interface ZLinkRouteChannelSendHandler {
  handle(payload: unknown, context: ZLinkRouteSendContext): Promise<void>;
}

export interface ZLinkRouteChannelRequestHandler {
  handle(payload: unknown, context: ZLinkRouteRequestContext): Promise<unknown>;
}

// These records describe the private composition bridge between the Nest
// adapter and the framework runtime. They intentionally remain owned by the
// Nest package instead of becoming a framework package subpath.
export interface ZLinkFrameworkRegistration {
  readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly codecs: ZLinkCodecRegistration;
  readonly requestTimeoutMs?: number;
  readonly actorFactories: ReadonlyMap<string, Type>;
  readonly actorTransferAdapters: ReadonlyMap<Type, Type>;
  readonly actorTransferForwardWindowMs: number;
  readonly spotFactories: ReadonlySet<Type<ZLinkSpot>>;
  readonly channels: ReadonlyMap<string, ZLinkChannelOptions>;
  readonly channelClients: ReadonlySet<string>;
  readonly fanoutPublishers: ReadonlySet<string>;
  readonly routeChannels: ReadonlySet<string>;
  readonly routeChannelOptions: ReadonlyMap<string, ZLinkRouteChannelOptions>;
  readonly streamNodes: ReadonlyMap<string, ZLinkStreamNodeOptions>;
  readonly streamCompression?: ZLinkStreamCompressionOptions;
  readonly spotNodes: ReadonlyMap<string, ZLinkSpotNodeOptions>;
  readonly spotPublisherClients: ReadonlySet<string>;
  readonly filterTypes: readonly Type<ZLinkHandlerFilter>[];
  readonly worker?: ZLinkWorkerOptions;
  readonly dispatch?: ZLinkDispatchOptions;
  readonly monitoring?: ZLinkMonitoringOptions;
  readonly metrics?: ZLinkMetricsOptions;
  readonly locations: ZLinkLocationRegistration;
}

export interface ZLinkLocationRegistration {
  readonly useInMemoryStores: boolean;
  readonly storeInstance?: ZLinkLocationStore;
  readonly options: ZLinkLocationOptions;
}

export interface ZLinkCodecSerializerRegistration {
  readonly contentType: string;
  readonly serializer: ZLinkMessageSerializer;
  readonly canSerialize?: (payloadType: Type) => boolean;
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
  readonly actorTransferAdapters?: ReadonlyMap<Type, Type>;
  readonly actorTransferForwardWindowMs?: number;
  readonly channels?: Readonly<Record<string, ZLinkChannelOptions>>;
  readonly routeChannels?: readonly (string | ZLinkRouteChannelOptions)[];
  readonly streamNodes?: Readonly<Record<string, ZLinkStreamNodeOptions>>;
  readonly streamCompression?: ZLinkStreamCompressionOptions;
  readonly spotNodes?: readonly (string | ZLinkSpotNodeRegistrationOptions)[] |
    Readonly<Record<string, ZLinkSpotNodeOptions>>;
  readonly spotPublisherClients?: readonly string[];
  readonly filters?: readonly Type<ZLinkHandlerFilter>[];
  readonly worker?: ZLinkWorkerOptions;
  readonly dispatch?: ZLinkDispatchOptions;
  readonly monitoring?: ZLinkMonitoringOptions;
  readonly metrics?: ZLinkMetricsOptions;
  readonly locations?: {
    readonly useInMemoryStores?: boolean;
    readonly storeInstance?: ZLinkLocationStore;
    readonly options?: ZLinkLocationOptions;
  };
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
    readonly maxMessageSize?: number;
  };
  readonly subscriber?: ZLinkClientCapabilityOptions;
}

export interface ZLinkClientCapabilityOptions {
  readonly manualConnections?: readonly string[];
  readonly sendHighWaterMark?: number;
  readonly receiveHighWaterMark?: number;
  readonly sendTimeoutMs?: number;
  readonly maxMessageSize?: number;
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
  readonly maxMessageSize?: number;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: readonly ZLinkRouteChannelHandlerOptions[];
}

export interface ZLinkRouteChannelOptions extends ZLinkRouteMeshChannelOptions {
  readonly routerChannelId: string;
}

export interface ZLinkStreamNodeOptions {
  readonly bind?: string;
  readonly tlsServer?: {
    readonly certificatePath: string;
    readonly keyPath: string;
    readonly requireClientCertificate?: boolean;
  };
  readonly session?: Type;
}

export interface ZLinkSpotNodeRegistrationOptions extends ZLinkSpotNodeOptions {
  readonly name: string;
}

export interface ZLinkSpotNodeOptions {
  readonly drainPolicy?: ZLinkSpotDrainPolicy;
  readonly routingId?: string;
  readonly router?: ZLinkSpotRouterCapabilityOptions;
  readonly pubSub?: ZLinkSpotPubSubCapabilityOptions;
  readonly entrySpot?: ZLinkEntrySpotOptions;
  readonly entrySpotType?: Type<ZLinkEntrySpot>;
  readonly spotFactories?: readonly Type<ZLinkSpot>[];
  readonly actorFactories?: Readonly<Record<string, Type> | Map<string, Type>>;
  readonly entrySpotTimerHandlers?: readonly ZLinkEntrySpotTimerHandlerRegistration[];
  readonly entrySpotPacketHandlers?: readonly ZLinkEntrySpotPacketHandlerRegistration[];
  readonly entrySpotSubscriptionHandlers?: readonly ZLinkEntrySpotSubscriptionHandlerRegistration[];
  readonly entrySpotActorSendHandlers?: readonly ZLinkEntrySpotActorSendHandlerRegistration[];
  readonly entrySpotActorRequestHandlers?: readonly ZLinkEntrySpotActorRequestHandlerRegistration[];
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotPacketHandlers?: readonly ZLinkSpotPacketHandlerRegistration[];
  readonly spotSubscriptionHandlers?: readonly ZLinkSpotSubscriptionHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
}

export interface ZLinkSpotRouterCapabilityOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly manualPeerConnections?: readonly { readonly peerRid: RoutingId; readonly endpoint: string }[];
  readonly routingId?: string;
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
  readonly handler: { handle(payload: unknown, context: ZLinkPublishContext): Promise<void> };
}

export interface ZLinkChannelRequestHandlerRegistration {
  readonly packetName: string;
  readonly handler: { handle(payload: unknown, context: ZLinkRequestContext): Promise<unknown> };
}

export interface ZLinkChannelSendHandlerRegistration {
  readonly packetName: string;
  readonly handler: { handle(payload: unknown, context: ZLinkSendContext): Promise<void> };
}

export interface ZLinkEntrySpotTimerHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>; readonly handlerType: Type;
  readonly name: string; readonly periodMs: number; readonly options?: ZLinkTimerOptions;
}
export interface ZLinkEntrySpotPacketHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>; readonly handlerType: Type; readonly packetName?: string;
}
export interface ZLinkEntrySpotSubscriptionHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>; readonly handlerType: Type; readonly topic: string;
}
export interface ZLinkEntrySpotActorSendHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>; readonly actorType: Type<ZLinkActor>;
  readonly handlerType: Type; readonly packetName: string;
}
export interface ZLinkEntrySpotActorRequestHandlerRegistration extends ZLinkEntrySpotActorSendHandlerRegistration {}
export interface ZLinkSpotTimerHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>; readonly handlerType: Type;
  readonly name: string; readonly periodMs: number; readonly options?: ZLinkTimerOptions;
}
export interface ZLinkSpotPacketHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>; readonly handlerType: Type; readonly packetName?: string;
}
export interface ZLinkSpotSubscriptionHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>; readonly handlerType: Type; readonly topic: string;
}
export interface ZLinkSpotActorSendHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>; readonly actorType: Type<ZLinkActor>;
  readonly handlerType: Type; readonly packetName: string;
}
export interface ZLinkSpotActorRequestHandlerRegistration extends ZLinkSpotActorSendHandlerRegistration {}
