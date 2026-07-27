import type { RoutingId, Type } from '../Common';
import type { ZLinkEntrySpot, ZLinkInstanceSpot, ZLinkSpot } from './ZLinkSpot';

export interface ZLinkSpotNodeBuilder {
  routingId(routingId: RoutingId): this;
  setRoutingIdPrefix(prefix: string): this;
  enableRouter(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
  connectRouter(endpoint: string): this;
  connectRouter(peerRid: RoutingId, endpoint: string): this;
  enablePubSub(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
  connectPeerPub(endpoint: string): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  addInstanceSpotFactory<TSpot extends ZLinkInstanceSpot>(
    instanceSpotType: string,
    implementation: Type<TSpot>
  ): this;
  actorFactory(actorType: string, factoryType: Type): this;
}
