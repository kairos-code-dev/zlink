import type { ZLinkDiscoveryBuilder } from '../Configuration';
import type { RoutingId, Type } from '../Common';
import type { ZLinkEntrySpot, ZLinkSpot } from './ZLinkSpot';

export interface ZLinkSpotNodeBuilder {
  enableRouter(): SpotRouterCapabilityBuilder;
  enablePubSub(): SpotPubSubCapabilityBuilder;
  configureEntrySpot(options: ZLinkEntrySpotOptions): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  attachChannelClient(channelName: string): SpotChannelClientCapabilityBuilder;
  attachSpotPublisherClient(channelName: string): SpotPublisherClientCapabilityBuilder;
  acceptSpotRoutesFromChannel(channelName: string): ZLinkSpotRouteChannelAcceptanceBuilder;
}

export interface ZLinkSpotMeshNodeBuilder extends ZLinkSpotNodeBuilder {}

export interface ZLinkSpotMeshBuilder {
  useDiscovery(): ZLinkDiscoveryBuilder;
  addNode(name: string): ZLinkSpotMeshNodeBuilder;
}

export interface SpotRouterCapabilityBuilder {
  bind(endpoint: string): this;
  routingId(routingId: RoutingId): this;
  connect(endpoint: string): this;
}

export interface SpotPubSubCapabilityBuilder {
  bind(endpoint: string): this;
  routingId(routingId: RoutingId): this;
  connect(endpoint: string): this;
}

export interface SpotPublisherClientCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface SpotChannelClientCapabilityBuilder {
  connect(endpoint: string): this;
}

export interface ZLinkSpotRouteChannelAcceptanceBuilder {
  connect(endpoint: string): this;
}

export interface ZLinkEntrySpotOptions {
  routingId?: RoutingId;
}
