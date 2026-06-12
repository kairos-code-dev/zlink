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
  readonly context?: ZLinkSpotContext;
  configure?(): void;
  onCreate?(request: Message, signal?: AbortSignal): Promise<ZLinkSpotCreateResponse>;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
  onActorJoin?(actor: TActor, request: Message, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onPostActorJoined?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onActorLeft?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onActorDisconnected?(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> {
  readonly context?: ZLinkEntrySpotContext;
  configure?(): void;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
  onPostActorJoined?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onActorLeft?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onActorDisconnected?(actor: TActor, signal?: AbortSignal): Promise<void>;
}
