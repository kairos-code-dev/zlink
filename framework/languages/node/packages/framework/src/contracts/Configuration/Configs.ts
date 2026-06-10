import type { RoutingId } from '../Common';

export interface ZLinkRegistrySpotRemoteAddressesOptions {
  registryEndpoint: string;
}

export interface ZLinkSocketConfig {
  bind?: string;
  connect?: string;
  channelName?: string;
}

export interface ZLinkRouteConfig {
  channelName: string;
  endpoint: string;
}

export interface ZLinkOutboundRouteConfig {
  targetNodeRid: RoutingId;
  endpoint: string;
}

export interface ZLinkSpotPublisherConfig {
  topic: string;
}

export interface ZLinkSpotSubscriberConfig {
  topic: string;
}
