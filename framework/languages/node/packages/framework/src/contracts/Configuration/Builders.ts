import type { ZLinkActor } from '../Actors';
import type { ZLinkActorTransferAdapter, ZLinkSpot, ZLinkSpotMeshBuilder } from '../Spots';
import type { ZLinkSession, ZLinkSessionFactory, ZLinkStreamCompressionCodec } from '../Streams';
import type { ZLinkEndpointConnections } from './Connections';
import type { ZLinkCodecRegistryBuilder } from '../Codecs';
import type { ZLinkDispatchOptionsBuilder } from '../Dispatch';
import type { ZLinkLocationStore, ZLinkLocationOptions } from '../Locations';
import type { Type } from '../Common';
import type { ZLinkWorkerOptions } from './RegistrationTypes';
import type { ZLinkSocketConfig } from './Configs';

export interface ZLinkFrameworkOptions {
  codecs(): ZLinkCodecRegistryBuilder;
  /**
   * Configures the bounded worker-thread pool used by `runCpuWorker(...)`.
   * I/O workers do not consume these threads.
   */
  configureWorker(options: ZLinkWorkerOptions): this;
  configureDispatch(): ZLinkDispatchOptionsBuilder;
  useInMemoryLocationStores(): this;
  addLocationStore(store: ZLinkLocationStore): this;
  addActorTransferAdapter<TActor extends ZLinkActor>(
    actorType: Type<TActor>,
    adapterType: Type<ZLinkActorTransferAdapter<TActor>>
  ): this;
  /** Overrides the 5 second source forwarding window for stale actor references. */
  setActorTransferForwardWindow(timeoutMs: number): this;
  configureLocations(): ZLinkLocationOptions;
  configureStreamCompression(): ZLinkStreamCompressionBuilder;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  addSpotMesh(channelName: string): ZLinkSpotMeshBuilder;
  addClientServerChannel(name: string): ZLinkClientServerChannelBuilder;
  addFanoutChannel(name: string): ZLinkFanoutChannelBuilder;
  addRouteMeshChannel(name: string): ZLinkRouteMeshChannelBuilder;
  addStreamNode(name: string): ZLinkStreamNodeBuilder;
}

export interface ZLinkMetadataPolicyBuilder {
  forward(enabled?: boolean): this;
}

export interface ZLinkStreamCompressionBuilder {
  useDefault(): this;
  useLz4(): this;
  use(codec: ZLinkStreamCompressionCodec): this;
  disable(): this;
}

export interface ZLinkClientServerChannelBuilder {
  enableServer(endpoint: string): this;
  routingId(routingId: string): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  configureServerSocket(): ZLinkSocketConfig;
  configureClientSocket(): ZLinkSocketConfig;
  enableClient(): this;
  enableClient(endpoint: string): this;
  clientConnections(): ZLinkEndpointConnections;
  setDefaultRequestTimeout(timeoutMs: number): this;
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

export interface ZLinkRouteMeshChannelBuilder {
  enableServer(endpoint: string): this;
  routingId(routingId: string): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  enableClient(): this;
  enableClient(endpoint: string): this;
  clientConnections(): ZLinkEndpointConnections;
  configureSocket(): ZLinkSocketConfig;
  setDefaultRequestTimeout(timeoutMs: number): this;
}

export interface ZLinkStreamNodeBuilder {
  bind(endpoint: string): this;
  setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}
