import type { ZLinkActor } from '../Actors';
import type { ZLinkTimerTick } from '../Timers';
import type {
  ZLinkHandlerContext,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkSendContext,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorSendContext
} from './Contexts';

export interface ZLinkRequestHandler<TRequest, TResponse> {
  handle(request: TRequest, context: ZLinkRequestContext): Promise<TResponse>;
}

export interface ZLinkSendHandler<TMessage> {
  handle(message: TMessage, context: ZLinkSendContext): Promise<void>;
}

export interface ZLinkRouteSendHandler<TMessage> {
  handle(message: TMessage, context: ZLinkRouteSendContext): Promise<void>;
}

export interface ZLinkRouteRequestHandler<TRequest, TReply> {
  handle(request: TRequest, context: ZLinkRouteRequestContext): Promise<TReply>;
}

export interface ZLinkPublishHandler<TMessage> {
  handle(message: TMessage, context: ZLinkPublishContext): Promise<void>;
}

export interface ZLinkSpotPacketHandler<TSpot, TMessage> {
  handle(spot: TSpot, message: TMessage, context: ZLinkHandlerContext): Promise<void>;
}

export interface ZLinkSpotRequestHandler<TSpot, TRequest, TReply> {
  handle(spot: TSpot, request: TRequest, context: ZLinkHandlerContext): Promise<TReply>;
}

export interface ZLinkSpotSubscriptionHandler<TSpot, TEvent> {
  handle(spot: TSpot, event: TEvent, context: ZLinkPublishContext): Promise<void>;
}

export interface ZLinkSpotTimerHandler<TSpot> {
  handle(spot: TSpot, tick: ZLinkTimerTick): Promise<void>;
}

export interface ZLinkSpotActorSendHandler<TSpot, TActor extends ZLinkActor, TMessage> {
  handle(spot: TSpot, actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkSpotActorRequestHandler<TSpot, TActor extends ZLinkActor, TRequest, TReply> {
  handle(spot: TSpot, actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export interface ZLinkSpotActorDisconnectedHandler<TSpot, TActor extends ZLinkActor> {
  handle(spot: TSpot, actor: TActor): Promise<void>;
}

export interface ZLinkEntrySpotActorSendHandler<TEntrySpot, TActor extends ZLinkActor, TMessage> {
  handle(entrySpot: TEntrySpot, actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkEntrySpotActorRequestHandler<TEntrySpot, TActor extends ZLinkActor, TRequest, TReply> {
  handle(entrySpot: TEntrySpot, actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export interface ZLinkEntrySpotActorDisconnectedHandler<TEntrySpot, TActor extends ZLinkActor> {
  handle(entrySpot: TEntrySpot, actor: TActor): Promise<void>;
}
