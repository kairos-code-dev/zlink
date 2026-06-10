import type { ZLinkActor } from '../Actors';
import type { ZLinkPublishCall, ZLinkRequestCall, ZLinkSendCall } from '../Channels';
import type { Message, RoutingId, Type } from '../Common';
import type { ZLinkSpotTimerHandler } from '../Handlers';
import type { ZLinkTimer, ZLinkTimerOptions } from '../Timers';
import type { ZLinkEntrySpot, ZLinkSpot } from './ZLinkSpot';

export interface ZLinkActorHandlerRegistry {
  addHandler(handlerType: Type): this;
  addActorPacket(handlerType: Type, actorType: Type<ZLinkActor>, packetName?: string): this;
  addActorDisconnected(handlerType: Type, actorType: Type<ZLinkActor>): this;
}

export interface ZLinkSpotHandlerRegistry extends ZLinkActorHandlerRegistry {
  addPacket(handlerType: Type, packetName?: string): this;
  addSubscribe(handlerType: Type, topic: string): this;
  addSpotHandler(handlerType: Type): this;
}

export interface ZLinkSpotContext {
  readonly spotRid: RoutingId;
  readonly nodeRid: RoutingId;
  readonly routingId: RoutingId;
  readonly handlers: ZLinkSpotHandlerRegistry;
  readonly outbound: ZLinkSpotOutbound;
  leaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  close(signal?: AbortSignal): Promise<boolean>;
  addTimer<THandler extends ZLinkSpotTimerHandler<ZLinkSpot>>(
    name: string,
    periodMs: number,
    handlerType: Type<THandler>,
    options?: ZLinkTimerOptions,
    signal?: AbortSignal
  ): Promise<ZLinkTimer>;
}

export interface ZLinkEntrySpotContext {
  readonly spotRid: RoutingId;
  readonly nodeRid: RoutingId;
  readonly routingId: RoutingId;
  readonly handlers: ZLinkSpotHandlerRegistry;
  readonly outbound: ZLinkSpotOutbound;
  addTimer<THandler extends ZLinkSpotTimerHandler<ZLinkEntrySpot>>(
    name: string,
    periodMs: number,
    handlerType: Type<THandler>,
    options?: ZLinkTimerOptions,
    signal?: AbortSignal
  ): Promise<ZLinkTimer>;
}

export interface ZLinkSpotActorReplyOptions {
  metadata(key: string, value: string): this;
  compress(enabled?: boolean): this;
}

export interface ZLinkSpotOutbound {
  sendToSpot<TMessage>(spotRid: RoutingId, message: TMessage): ZLinkSendCall;
  requestToSpot<TRequest>(spotRid: RoutingId, request: TRequest): ZLinkRequestCall;
  publish<TEvent>(topic: string, event: TEvent): ZLinkPublishCall;
  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall;
  requestToChannel<TRequest>(channelName: string, request: TRequest): ZLinkRequestCall;
}

export enum ZLinkSpotCreateState {
  Existing = 'existing',
  Created = 'created',
  Rejected = 'rejected'
}

export interface ZLinkSpotCreateResult {
  readonly spotRid: RoutingId;
  readonly state: ZLinkSpotCreateState;
  readonly reply?: Message;
}

export interface ZLinkSpotInfo {
  readonly spotRid: RoutingId;
}

export interface ZLinkSpotManager {
  create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    request?: Message,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request?: Message,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  find(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotInfo | null>;
  list(signal?: AbortSignal): Promise<readonly ZLinkSpotInfo[]>;
  close(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean>;
}
