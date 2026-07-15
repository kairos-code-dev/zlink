import type { RoutingId, Type } from '../Common';
import type { ZLinkEntrySpot, ZLinkSpot } from './ZLinkSpot';
import type { ZLinkSpotDrainPolicy } from '../Eventing';

export interface ZLinkSpotNodeBuilder {
  routingId(routingId: RoutingId): this;
  useAllocatedRoutingId(slotCount: number, routingIdPrefix?: string): this;
  setRoutingIdAllocationGroup(groupName: string): this;
  enableRouter(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
  connectRouter(endpoint: string): this;
  connectRouter(peerRid: RoutingId, endpoint: string): this;
  enablePubSub(endpoint: string, routingId?: RoutingId, connect?: string | readonly string[]): this;
  connectPeerPub(endpoint: string): this;
  configureEntrySpot(options: ZLinkEntrySpotOptions): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
  actorFactory(actorType: string, factoryType: Type): this;
  useDrainPolicy(policy: ZLinkSpotDrainPolicy): this;
}

export interface ZLinkSpotMeshNodeBuilder extends ZLinkSpotNodeBuilder {}

export interface ZLinkSpotMeshBuilder extends ZLinkSpotNodeBuilder {}

export interface ZLinkEntrySpotOptions {
  routingId?: RoutingId;
}
