import type { ZLinkSpot, ZLinkSpotMeshBuilder, ZLinkSpotNodeBuilder } from '../Spots';
import type { ZLinkSession } from '../Streams';
import type { Type } from '../Common';

export interface ZLinkFrameworkOptions {
  useDiscovery(): ZLinkDiscoveryBuilder;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  addSpotMesh(channelName: string): ZLinkSpotMeshBuilder;
  addClientServerChannel(name: string): ZLinkClientServerChannelBuilder;
  addFanoutChannel(name: string): ZLinkFanoutChannelBuilder;
  addDealerMeshChannel(name: string): ZLinkDealerMeshChannelBuilder;
  addRouteChannel(name: string): ZLinkRouteChannelBuilder;
  addRouteMeshChannel(name: string): ZLinkRouteMeshChannelBuilder;
  addStreamNode(name: string): ZLinkStreamNodeBuilder;
  addSpotNode(name: string): ZLinkSpotNodeBuilder;
}

export interface ZLinkDiscoveryBuilder {
  addRegistryEndpoint(endpoint: string): this;
}

export interface ZLinkMetadataPolicyBuilder {
  forward(enabled?: boolean): this;
}

export interface ChannelServerCapabilityBuilder {
  bind(endpoint: string): this;
}

export interface ChannelClientCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface DealerMeshChannelClientCapabilityBuilder extends ChannelClientCapabilityBuilder {}

export interface ChannelPublisherCapabilityBuilder {
  bind(endpoint: string): this;
}

export interface ChannelSubscriberCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface ZLinkClientServerChannelBuilder {
  enableServer(): ChannelServerCapabilityBuilder;
  enableClient(): ChannelClientCapabilityBuilder;
}

export interface ZLinkFanoutChannelBuilder {
  enablePublisher(): ChannelPublisherCapabilityBuilder;
  enableSubscriber(): ChannelSubscriberCapabilityBuilder;
}

export interface ZLinkDealerMeshChannelBuilder {
  enableClient(): DealerMeshChannelClientCapabilityBuilder;
}

export interface ZLinkRouteChannelBuilder {
  enableRouter(): ChannelServerCapabilityBuilder;
  enableDealer(): ChannelClientCapabilityBuilder;
}

export interface ZLinkRouteMeshChannelBuilder extends ZLinkRouteChannelBuilder {}

export interface ZLinkStreamNodeBuilder {
  bind(endpoint: string): this;
  attachActorGateway(spotNodeName: string): this;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession>): this;
}
