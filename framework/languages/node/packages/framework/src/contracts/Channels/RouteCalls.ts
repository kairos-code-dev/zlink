import type { RoutingId } from '../Common';
import type { ZLinkPublishCall, ZLinkSendCall } from './Calls';

export interface ZLinkRouteRequestCall {
  packetName(packetName: string): this;
  timeout(timeoutMs: number): this;
  submit<TReply>(signal?: AbortSignal): Promise<TReply>;
}

export interface ZLinkRouteClient {
  send(routerChannelId: string, targetNodeRid: RoutingId, message: unknown): ZLinkSendCall;
  request(routerChannelId: string, targetNodeRid: RoutingId, request: unknown): ZLinkRouteRequestCall;
}

export interface ZLinkSpotPublisherClient {
  publishSpot(channelName: string, topic: string, event: unknown): ZLinkPublishCall;
}
