import type { ZLinkDiscoveryBuilder } from '../Configuration';
import type { RoutingId, Type } from '../Common';
import type { ZLinkEntrySpot, ZLinkSpot } from './ZLinkSpot';

export interface ZLinkSpotNodeBuilder {
  enableRouter(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
  connectRouter(endpoint: string): this;
  connectRouter(peerRid: RoutingId, endpoint: string): this;
  routerRoutingId(routingId: RoutingId): this;
  enablePubSub(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
  connectPeerPub(endpoint: string): this;
  connectPubSub(endpoint: string): this;
  pubSubRoutingId(routingId: RoutingId): this;
  configureEntrySpot(options: ZLinkEntrySpotOptions): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
}

export interface ZLinkSpotMeshNodeBuilder extends ZLinkSpotNodeBuilder {}

export interface ZLinkSpotMeshBuilder extends ZLinkSpotNodeBuilder {
  useDiscovery(): ZLinkDiscoveryBuilder;
}

export interface ZLinkEntrySpotOptions {
  routingId?: RoutingId;
}
