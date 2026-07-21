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

export interface ZLinkSpotActorSendHandler<TActor extends ZLinkActor, TMessage> {
  handle(actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkSpotActorRequestHandler<TActor extends ZLinkActor, TRequest, TReply> {
  handle(actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}

export interface ZLinkEntrySpotActorSendHandler<TActor extends ZLinkActor, TMessage> {
  handle(actor: TActor, context: ZLinkSpotActorSendContext, message: TMessage): Promise<void>;
}

export interface ZLinkEntrySpotActorRequestHandler<TActor extends ZLinkActor, TRequest, TReply> {
  handle(actor: TActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TReply>;
}
