import type { RoutingId } from '../Common';

export interface ZLinkSpotRemoteAddressResolver {
  resolve(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotRemoteAddress>;
}

export enum ZLinkSpotKind {
  Invalid = 0,
  Entry = 1,
  User = 2
}

export interface ZLinkSpotRemoteAddress {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind: ZLinkSpotKind;
}
