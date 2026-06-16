import type { Message, RoutingId } from '../Common';
import type { ZLinkPublishCall, ZLinkRequestCall, ZLinkSendCall } from './Calls';

export interface ZLinkRouteClient {
  send(routerChannelId: string, targetNodeRid: RoutingId, message: Message): ZLinkSendCall;
  request(routerChannelId: string, targetNodeRid: RoutingId, request: Message): ZLinkRequestCall;
}

export interface ZLinkSpotPublisherClient {
  publishSpot(channelName: string, topic: string, event: Message): ZLinkPublishCall;
}
