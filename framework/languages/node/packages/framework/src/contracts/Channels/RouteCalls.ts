import type { RoutingId } from '../Common';
import type { ZLinkPublishCall, ZLinkRequestCall, ZLinkSendCall } from './Calls';

export interface ZLinkRouteClient {
  send(routerChannelId: string, targetNodeRid: RoutingId, message: unknown): ZLinkSendCall;
  request(routerChannelId: string, targetNodeRid: RoutingId, request: unknown): ZLinkRequestCall;
}

export interface ZLinkSpotPublisherClient {
  publishSpot(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}
