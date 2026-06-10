import type { RoutingId } from '../Common';
import type { ZLinkPublishCall, ZLinkRequestCall, ZLinkSendCall } from './Calls';

export interface ZLinkRouteClient {
  send<TMessage>(routerChannelId: string, targetNodeRid: RoutingId, message: TMessage): ZLinkSendCall;
  request<TRequest>(routerChannelId: string, targetNodeRid: RoutingId, request: TRequest): ZLinkRequestCall;
}

export interface ZLinkSpotPublisherClient {
  publishSpot<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall;
}
