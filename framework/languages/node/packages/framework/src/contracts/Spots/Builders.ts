import type { ZLinkDiscoveryBuilder } from '../Configuration';
import type { RoutingId, Type } from '../Common';
import type { ZLinkEntrySpot, ZLinkSpot } from './ZLinkSpot';

export interface ZLinkSpotNodeBuilder {
  enableRouter(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
  connectRouter(endpoint: string): this;
  routerRoutingId(routingId: RoutingId): this;
  enablePubSub(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
  connectPubSub(endpoint: string): this;
  pubSubRoutingId(routingId: RoutingId): this;
  configureEntrySpot(options: ZLinkEntrySpotOptions): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  attachChannelClient(channelName: string, endpoint?: string | readonly string[]): this;
  attachSpotPublisherClient(channelName: string, endpoint?: string | readonly string[]): this;
  acceptSpotRoutesFromChannel(channelName: string, endpoint?: string | readonly string[]): this;
}

export interface ZLinkSpotMeshNodeBuilder extends ZLinkSpotNodeBuilder {}

export interface ZLinkSpotMeshBuilder {
  useDiscovery(): ZLinkDiscoveryBuilder;
  addNode(name: string): ZLinkSpotMeshNodeBuilder;
}

export interface ZLinkEntrySpotOptions {
  routingId?: RoutingId;
}
