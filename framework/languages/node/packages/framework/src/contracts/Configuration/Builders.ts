import type { ZLinkSpot, ZLinkSpotMeshBuilder } from '../Spots';
import type { ZLinkSession, ZLinkSessionFactory, ZLinkStreamCompressionCodec } from '../Streams';
import type { ZLinkCodecRegistryBuilder } from '../Codecs';
import type { ZLinkDispatchOptionsBuilder } from '../Dispatch';
import type { IZLinkLocationStore, ZLinkLocationOptions } from '../Locations';
import type { Type } from '../Common';
import type { ZLinkWorkerOptions } from './Registration';
import type { ZLinkSocketConfig } from './Configs';

export interface ZLinkFrameworkOptions {
  codecs(): ZLinkCodecRegistryBuilder;
  /**
   * Configures the single worker offload pool used by `runWorker(...)`.
   * On Node these settings bound the asynchronous deferral (in-flight and
   * pending-queue limits); they do not enable CPU thread offload.
   */
  configureWorker(options: ZLinkWorkerOptions): this;
  configureDispatch(): ZLinkDispatchOptionsBuilder;
  useInMemoryLocationStores(): this;
  addLocationStore(store: IZLinkLocationStore): this;
  configureLocations(): ZLinkLocationOptions;
  configureStreamCompression(): ZLinkStreamCompressionBuilder;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  addSpotMesh(channelName: string): ZLinkSpotMeshBuilder;
  addClientServerChannel(name: string): ZLinkClientServerChannelBuilder;
  addFanoutChannel(name: string): ZLinkFanoutChannelBuilder;
  addRouteChannel(name: string): ZLinkRouteChannelBuilder;
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
  configureServerSocket(): ZLinkSocketConfig;
  configureClientSocket(): ZLinkSocketConfig;
  enableClient(): this;
  enableClient(endpoint: string): this;
  setDefaultRequestTimeout(timeoutMs: number): this;
}

export interface ZLinkFanoutChannelBuilder {
  enablePublisher(endpoint: string): this;
  enableSubscriber(): this;
  enableSubscriber(endpoint: string): this;
}

export interface ZLinkRouteChannelBuilder {
  enableServer(endpoint: string): this;
  enableClient(): this;
  enableClient(endpoint: string): this;
  configureSocket(): ZLinkSocketConfig;
  setDefaultRequestTimeout(timeoutMs: number): this;
}

export interface ZLinkRouteMeshChannelBuilder extends ZLinkRouteChannelBuilder {}

export interface ZLinkStreamNodeBuilder {
  bind(endpoint: string): this;
  setTlsServer(certificatePath: string, keyPath: string, requireClientCertificate?: boolean): this;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}
