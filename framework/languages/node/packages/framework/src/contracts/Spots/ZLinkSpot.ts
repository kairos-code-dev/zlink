import type { ZLinkActor } from '../Actors';
import type { Message } from '../Common';
import type { ZLinkEntrySpotContext, ZLinkSpotContext } from './Contracts';

export interface ZLinkSpotActorJoinResponse {
  readonly accepted: boolean;
  readonly reply?: Message;
}

export interface ZLinkSpotCreateResponse {
  readonly accepted: boolean;
  readonly reply?: Message;
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> {
  readonly context?: ZLinkSpotContext<TActor>;
  configure?(): void | Promise<void>;
  onCreate?(request: Message, signal?: AbortSignal): Promise<ZLinkSpotCreateResponse>;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
  onActorJoin?(actor: TActor, request: Message, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onJoinActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onLeaveActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onDisconnectActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> {
  readonly context?: ZLinkEntrySpotContext<TActor>;
  configure?(): void | Promise<void>;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
  onCreateActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onJoinActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onLeaveActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onDisconnectActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
}
