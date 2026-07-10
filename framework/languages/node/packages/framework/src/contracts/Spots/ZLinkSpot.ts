import type { ZLinkActor } from '../Actors';
import type { ZLinkMessage } from '../Common';
import type { ZLinkEntrySpotContext, ZLinkSpotContext } from './Contracts';

export interface ZLinkSpotAcceptRejectResponse {
  readonly accepted: boolean;
  readonly reply?: unknown;
}

export interface ZLinkSpotActorJoinResponse extends ZLinkSpotAcceptRejectResponse {}

export interface ZLinkSpotCreateResponse extends ZLinkSpotAcceptRejectResponse {}

export interface ZLinkSpotActorLifecycle<TActor extends ZLinkActor = ZLinkActor> {
  onActorJoin?(actorId: string, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onLeaveActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
  onDisconnectActor?(actor: TActor, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
  readonly context?: ZLinkSpotContext<TActor>;
  configure?(): void | Promise<void>;
  onCreate?(request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotCreateResponse>;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
  readonly context?: ZLinkEntrySpotContext<TActor>;
  configure?(): void | Promise<void>;
  onInitialize?(signal?: AbortSignal): Promise<void>;
  onClosing?(signal?: AbortSignal): Promise<void>;
  onCreateActor?(actor: TActor, createRequest: ZLinkMessage, signal?: AbortSignal): Promise<void>;
}
