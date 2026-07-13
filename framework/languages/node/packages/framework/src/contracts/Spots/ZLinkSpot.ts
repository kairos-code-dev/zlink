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
  onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor(actor: TActor): Promise<void>;
  onLeaveActor(actor: TActor): Promise<void>;
  onDisconnectActor?(actor: TActor): Promise<void>;
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
  readonly context: ZLinkSpotContext<TActor>;
  configure?(): void;
  onCreate?(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse>;
  onInitialize?(): Promise<void>;
  onClosing?(): Promise<void>;
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle<TActor> {
  readonly context: ZLinkEntrySpotContext<TActor>;
  configure?(): void;
  onInitialize?(): Promise<void>;
  onClosing?(): Promise<void>;
  onCreateActor?(actor: TActor, createRequest: ZLinkMessage): Promise<void>;
}
