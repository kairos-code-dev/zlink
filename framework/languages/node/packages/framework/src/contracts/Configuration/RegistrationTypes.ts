import type {
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkHandlerFilter,
  ZLinkInstanceSpot,
  ZLinkMonitoringOptions,
  ZLinkMetricsOptions,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkSendContext,
  ZLinkSpot,
  ZLinkStreamCompressionOptions,
  ZLinkTimerOptions
} from '../../contracts';
import type { ZLinkMessageSerializer } from '../Codecs';
import type { ZLinkDispatchOptions } from '../Dispatch';
import type { ZLinkLocationStore, ZLinkRelocationStore } from '../Locations';
import type { ZLinkLocationOptionValues } from '../RouteMesh';
import type {
  ZLinkObjectPlacementOptions,
  ZLinkRelocationPolicy
} from './ObjectRoles';

export interface ZLinkFrameworkRegistration {
  readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly codecs: ZLinkCodecRegistration;
  readonly requestTimeoutMs?: number;
  readonly actorFactories: ReadonlyMap<string, Type>;
  readonly actorTransferAdapters: ReadonlyMap<Type, Type>;
  readonly actorTransferTimeoutMs?: number;
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
  readonly relocationStoreInstance?: ZLinkRelocationStore;
  readonly options: Partial<ZLinkLocationOptionValues>;
}

/**
 * CPU worker-thread pool settings. I/O workers do not consume this pool.
 * `maxThreads` bounds active CPU jobs and `maxQueueLength` bounds queued jobs.
 */
export interface ZLinkWorkerOptions {
  readonly minThreads: number;
  readonly maxThreads: number;
  readonly idleTimeoutMs: number;
  readonly maxQueueLength: number;
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
  readonly actorTransferTimeoutMs?: number;
  /** How long a source node forwards packets sent to an actor's old generation. Defaults to 5000 ms. */
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
    readonly relocationStoreInstance?: ZLinkRelocationStore;
    readonly options?: Partial<ZLinkLocationOptionValues>;
  };
}

export interface ZLinkChannelOptions {
  readonly routingId?: string;
  readonly routingIdAllocation?: ZLinkRoutingIdAllocationOptions;
  readonly requestTimeoutMs?: number;
  readonly client?: ZLinkClientCapabilityOptions;
  readonly publisher?: ZLinkPublisherCapabilityOptions;
  readonly routeMesh?: ZLinkRouteMeshChannelOptions;
  readonly publishHandlers?: readonly ZLinkChannelPublishHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkChannelRequestHandlerRegistration[];
  readonly sendHandlers?: readonly ZLinkChannelSendHandlerRegistration[];
  readonly server?: {
    readonly bind?: string;
    readonly bindHost?: string;
    readonly advertiseHost?: string;
    readonly port?: number;
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

export interface ZLinkRoutingIdAllocationOptions {
  readonly slotCount: number;
  readonly routingIdPrefix: string;
  readonly groupName?: string;
}

export interface ZLinkRouteMeshChannelOptions {
  readonly requestTimeoutMs?: number;
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly routingIdAllocation?: ZLinkRoutingIdAllocationOptions;
  readonly weight?: number;
  readonly sendHighWaterMark?: number;
  readonly receiveHighWaterMark?: number;
  readonly sendTimeoutMs?: number;
  readonly maxMessageSize?: number;
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
  readonly routingIdAllocation?: ZLinkRoutingIdAllocationOptions;
  readonly weight?: number;
  readonly sendHighWaterMark?: number;
  readonly receiveHighWaterMark?: number;
  readonly sendTimeoutMs?: number;
  readonly maxMessageSize?: number;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: readonly ZLinkRouteChannelHandlerOptions[];
}

export interface ZLinkStreamNodeOptions {
  readonly bind?: string;
  readonly actorDispatchMeshName?: string;
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
  readonly objectRole?: 'client' | 'server';
  readonly placementWeight?: number;
  readonly maxActiveObjects?: number;
  readonly maxPendingActivations?: number;
  readonly routingId?: string;
  readonly routingIdAllocation?: ZLinkRoutingIdAllocationOptions;
  readonly router?: ZLinkSpotRouterCapabilityOptions;
  readonly pubSub?: ZLinkSpotPubSubCapabilityOptions;
  readonly entrySpot?: ZLinkEntrySpotOptions;
  readonly entrySpotType?: Type<ZLinkEntrySpot>;
  readonly spotFactories?: readonly Type<ZLinkSpot>[];
  readonly spotFactoryRegistrations?: Readonly<
    Record<string, ZLinkObjectFactoryRegistration<ZLinkSpot>>
  >;
  readonly instanceSpotFactories?: Readonly<Record<string, Type<ZLinkInstanceSpot>>>;
  readonly instanceSpotFactoryRegistrations?: Readonly<
    Record<string, ZLinkObjectFactoryRegistration<ZLinkInstanceSpot>>
  >;
  readonly actorFactories?: Readonly<Record<string, Type> | Map<string, Type>>;
  readonly actorFactoryRegistrations?: Readonly<
    Record<string, ZLinkObjectFactoryRegistration<ZLinkActor>>
  >;
  readonly meshChannels?: Readonly<Record<string, ZLinkMeshChannelOptions>>;
  readonly routeSendHandlers?: readonly ZLinkRouteMeshSendHandlerRegistration[];
  readonly routeRequestHandlers?: readonly ZLinkRouteMeshRequestHandlerRegistration[];
  readonly requestTimeoutMs?: number;
  readonly publisherConfig?: {
    readonly sendHighWaterMark?: number;
    readonly sendTimeoutMs?: number;
    readonly lingerMs?: number;
  };
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

export interface ZLinkObjectFactoryRegistration<T> {
  readonly implementation: Type<T>;
  readonly placement?: ZLinkObjectPlacementOptions;
  readonly relocation: ZLinkRelocationPolicy<T>;
}

export interface ZLinkMeshChannelOptions {
  readonly weight?: number;
  readonly sendHandlers?: readonly ZLinkMeshSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkMeshRequestHandlerRegistration[];
}

export interface ZLinkMeshSendHandlerRegistration {
  readonly packetName: string;
  readonly handlerType: Type;
}

export interface ZLinkMeshRequestHandlerRegistration {
  readonly packetName: string;
  readonly handlerType: Type;
}

export interface ZLinkRouteMeshSendHandlerRegistration {
  readonly packetName: string;
  readonly handlerType: Type;
}

export interface ZLinkRouteMeshRequestHandlerRegistration {
  readonly packetName: string;
  readonly handlerType: Type;
}

export interface ZLinkEntrySpotTimerHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly handlerType: Type;
  readonly name: string;
  readonly periodMs: number;
  readonly options?: ZLinkTimerOptions;
}

export interface ZLinkEntrySpotPacketHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly handlerType: Type;
  readonly packetName?: string;
}

export interface ZLinkEntrySpotSubscriptionHandlerRegistration {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly handlerType: Type;
  readonly channelName: string;
  readonly topic: string;
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

export interface ZLinkSpotPacketHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>;
  readonly handlerType: Type;
  readonly packetName?: string;
}

export interface ZLinkSpotSubscriptionHandlerRegistration {
  readonly spotType: Type<ZLinkSpot>;
  readonly handlerType: Type;
  readonly channelName: string;
  readonly topic: string;
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
  readonly handler?: {
    handle(payload: unknown, context: ZLinkRequestContext): Promise<unknown>;
  };
  readonly handlerType?: Type;
}

export interface ZLinkChannelSendHandlerRegistration {
  readonly packetName: string;
  readonly handler?: {
    handle(payload: unknown, context: ZLinkSendContext): Promise<void>;
  };
  readonly handlerType?: Type;
}

export interface ZLinkChannelPublishHandler {
  handle(payload: unknown, context: ZLinkPublishContext): Promise<void>;
}

export interface ZLinkRouteChannelSendHandler {
  handle(payload: unknown, context: ZLinkRouteSendContext): Promise<void>;
}

export interface ZLinkRouteChannelRequestHandler {
  handle(payload: unknown, context: ZLinkRouteRequestContext): Promise<unknown>;
}
