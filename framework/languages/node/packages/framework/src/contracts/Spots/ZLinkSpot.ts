import type { ZLinkActor } from '../Actors';
import type { ZLinkMessage } from '../Common';
import type { ZLinkActorJoinRequest, ZLinkActorMembership } from '../RouteMesh';
import type { ZLinkEntrySpotContext, ZLinkSpotContext } from './Contracts';

export interface ZLinkSpotAcceptRejectResponse {
  readonly accepted: boolean;
  readonly reply?: unknown;
}

export interface ZLinkSpotActorJoinResponse extends ZLinkSpotAcceptRejectResponse {}

export interface ZLinkSpotCreateResponse extends ZLinkSpotAcceptRejectResponse {}

export interface ZLinkSpotActorLifecycle {
  onActorJoin(actor: ZLinkActorJoinRequest, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor(actor: ZLinkActorMembership): Promise<void>;
  onLeaveActor(actor: ZLinkActorMembership): Promise<void>;
  onDisconnectActor(actor: ZLinkActorMembership): Promise<void>;
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle {
  readonly context: ZLinkSpotContext<TActor>;
  configure?(): void;
  onCreate?(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse>;
  onInitialize?(): Promise<void>;
  onClosing?(): Promise<void>;
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle {
  readonly context: ZLinkEntrySpotContext<TActor>;
  configure?(): void;
  onInitialize?(): Promise<void>;
  onClosing?(): Promise<void>;
  onCreateActor?(actor: ZLinkActorMembership, createRequest: ZLinkMessage): Promise<void>;
}
