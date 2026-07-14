import type { RoutingId } from '../Common';
import type { SpotHandle } from '../Spots';
import type { ZLinkPublishCall, ZLinkRequestCall, ZLinkSendCall } from './Calls';

export interface ZLinkRouteClient {
  sendToNode(routerChannelId: string, targetNodeRid: RoutingId, message: unknown): ZLinkSendCall;
  requestToNode(routerChannelId: string, targetNodeRid: RoutingId, request: unknown): ZLinkRequestCall;
  sendToSpot(spot: SpotHandle, message: unknown): ZLinkSendCall;
  requestToSpot(spot: SpotHandle, request: unknown): ZLinkRequestCall;
}

export interface ZLinkSpotPublisherClient {
  publish(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}
