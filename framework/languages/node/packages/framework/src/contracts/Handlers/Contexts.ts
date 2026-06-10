import type { RoutingId, ZLinkMessageMetadata } from '../Common';
import type { ZLinkSpotActorReplyOptions } from '../Spots';

export interface ZLinkHandlerContext {
  readonly channelName?: string;
  readonly packetName?: string;
  readonly contentType?: string;
  readonly connectionAborted?: AbortSignal;
}

export interface ZLinkRequestContext extends ZLinkHandlerContext {}
export interface ZLinkSendContext extends ZLinkHandlerContext {}
export interface ZLinkPublishContext extends ZLinkHandlerContext {
  readonly topic: string;
  readonly source?: string;
}

export interface ZLinkRouteSendContext extends ZLinkHandlerContext {
  readonly sourceNodeRid: RoutingId;
  readonly sourcePeerRid: RoutingId;
}

export interface ZLinkRouteRequestContext extends ZLinkRouteSendContext {
  readonly requestSeq: bigint;
}

export interface ZLinkSpotActorSendContext extends ZLinkHandlerContext {
  readonly metadata: ZLinkMessageMetadata;
}

export interface ZLinkSpotActorRequestContext extends ZLinkSpotActorSendContext {
  readonly reply: ZLinkSpotActorReplyOptions;
}
