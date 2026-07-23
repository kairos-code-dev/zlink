import type { ZLinkActor } from '../Actors';
import type {
  ZLinkActorTransferAdapter,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkSpot
} from '../Spots';
import type { ZLinkSession, ZLinkSessionFactory, ZLinkStreamCompressionCodec } from '../Streams';
import type { ZLinkEndpointConnections } from './Connections';
import type { ZLinkCodecRegistryBuilder } from '../Codecs';
import type { ZLinkDispatchOptionsBuilder } from '../Dispatch';
import type {
  ZLinkLocationOptions,
  ZLinkLocationStore,
  ZLinkRelocationStore
} from '../Locations';
import type { RoutingId, Type } from '../Common';
import type { ZLinkWorkerOptions } from './RegistrationTypes';
import type { ZLinkSpotPublisherConfig } from './Configs';
import type {
  ZLinkRequestHandler,
  ZLinkRouteRequestHandler,
  ZLinkRouteSendHandler,
  ZLinkSendHandler
} from '../Handlers';

export interface ZLinkFrameworkOptions {
  codecs(): ZLinkCodecRegistryBuilder;
  /**
   * Configures the bounded worker-thread pool used by `runCpuWorker(...)`.
   * I/O workers do not consume these threads.
   */
  configureWorker(options: ZLinkWorkerOptions): this;
  configureDispatch(): ZLinkDispatchOptionsBuilder;
  addLocationStore(store: ZLinkLocationStore): this;
  addRelocationStore(store: ZLinkRelocationStore): this;
  setActorTransferTimeout(timeoutMs: number): this;
  /** Overrides the 5 second source forwarding window for stale actor references. */
  setActorTransferForwardWindow(timeoutMs: number): this;
  configureLocations(): ZLinkLocationOptions;
  configureStreamCompression(): ZLinkStreamCompressionBuilder;
  addRouteMesh(meshName: string): ZLinkMeshNodeBuilder;
  addFanoutChannel(name: string): ZLinkFanoutChannelBuilder;
  addStreamNode(name: string): ZLinkStreamNodeBuilder;
}

export interface ZLinkMeshPeerConnection {
  readonly endpoint: string;
  readonly expectedRoutingId?: RoutingId;
}

export interface ZLinkMeshPeerConnections {
  connect(endpoint: string): void;
  connect(expectedRoutingId: RoutingId, endpoint: string): void;
  disconnect(endpoint: string): void;
  listConnections(): readonly ZLinkMeshPeerConnection[];
}

export interface ZLinkMeshChannelBuilder {
  setWeight(weight: number): this;
  addSendHandler<TMessage>(handlerType: Type<ZLinkSendHandler<TMessage>>): this;
  addRequestHandler<TRequest, TReply>(handlerType: Type<ZLinkRequestHandler<TRequest, TReply>>): this;
}

export interface ZLinkMeshNodeSocketConfig {
  maxMessageSize: number;
  sendHighWaterMark: number;
  receiveHighWaterMark: number;
  receiveTimeoutMs?: number;
  sendTimeoutMs?: number;
}

export interface ZLinkMeshNodeBuilder {
  channelName(channelName: string): ZLinkMeshChannelBuilder;
  listen(endpoint: string): this;
  routingId(routingId: RoutingId): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  configureRouterSocket(): ZLinkMeshNodeSocketConfig;
  configureSpotPublisher(): ZLinkSpotPublisherConfig;
  peerConnections(): ZLinkMeshPeerConnections;
  setDefaultRequestTimeout(timeoutMs: number): this;
  addRouteSendHandler<TMessage>(handlerType: Type<ZLinkRouteSendHandler<TMessage>>): this;
  addRouteRequestHandler<TRequest, TReply>(handlerType: Type<ZLinkRouteRequestHandler<TRequest, TReply>>): this;
  configureEntrySpot(options: ZLinkEntrySpotOptions): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  actorFactory(actorType: string, factoryType: Type): this;
  addActorTransferAdapter<TActor extends ZLinkActor>(
    actorType: string,
    adapterType: Type<ZLinkActorTransferAdapter<TActor>>
  ): this;
}

export interface ZLinkMetadataPolicyBuilder {
  allowSessionToActor(key: string): this;
  allowActorToSession(key: string): this;
}

export interface ZLinkStreamCompressionBuilder {
  useDefault(): this;
  useLz4(): this;
  use(codec: ZLinkStreamCompressionCodec): this;
  disable(): this;
}

export interface ZLinkFanoutChannelBuilder {
  enablePublisher(endpoint: string): this;
  routingId(routingId: string): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  enableSubscriber(): this;
  enableSubscriber(endpoint: string): this;
  subscriberConnections(): ZLinkEndpointConnections;
}

export interface ZLinkStreamNodeBuilder {
  bind(endpoint: string): this;
  enableActorDispatch(meshName: string): this;
  setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}
