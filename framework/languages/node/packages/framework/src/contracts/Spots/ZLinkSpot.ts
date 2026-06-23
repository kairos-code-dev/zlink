import type { ZLinkActor } from '../Actors';
import type { ZLinkMessage } from '../Common';
import type { ZLinkEntrySpotContext, ZLinkSpotContext } from './Contracts';

export interface ZLinkSpotActorJoinResponse {
  readonly accepted: boolean;
  readonly reply?: unknown;
}

export interface ZLinkSpotCreateResponse {
  readonly accepted: boolean;
  readonly reply?: unknown;
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> {
  readonly context?: ZLinkSpotContext<TActor>;
  configure?(): void | Promise<void>;
  onCreate?(request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotCreateResponse>;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
  onActorJoin?(actor: TActor, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onLeaveActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onDisconnectActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> {
  readonly context?: ZLinkEntrySpotContext<TActor>;
  configure?(): void | Promise<void>;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
  onCreateActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onActorJoin?(actor: TActor, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onLeaveActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onDisconnectActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
}
