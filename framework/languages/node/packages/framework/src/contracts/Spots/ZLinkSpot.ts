import type { ZLinkActor } from '../Actors';
import type { ZLinkMessage } from '../Common';
import type { ZLinkActorJoinRequest, ZLinkActorMembership } from '../RouteMesh';
import type {
  ZLinkEntrySpotContext,
  ZLinkInstanceSpotContext,
  ZLinkSpotContext
} from './Contracts';

export interface ZLinkSpotAcceptRejectResponse {
  readonly accepted: boolean;
  readonly reply?: unknown;
}

export interface ZLinkSpotActorJoinResponse extends ZLinkSpotAcceptRejectResponse {}

export interface ZLinkSpotCreateResponse extends ZLinkSpotAcceptRejectResponse {}

export enum ZLinkSpotCloseReason {
  ExplicitClose = 0,
  HostShutdown = 1,
  RelocationOut = 2
}

export interface ZLinkSpotClosingContext {
  readonly reason: ZLinkSpotCloseReason;
  readonly deadline: Date;
}

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
  onClosing?(context: ZLinkSpotClosingContext, cleanupSignal: AbortSignal): Promise<void>;
}

export interface ZLinkInstanceSpot {
  readonly context: ZLinkInstanceSpotContext;
  configure?(): void;
  onInitialize?(): Promise<void>;
  onClosing?(context: ZLinkSpotClosingContext, cleanupSignal: AbortSignal): Promise<void>;
}

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkSpotActorLifecycle {
  readonly context: ZLinkEntrySpotContext<TActor>;
  configure?(): void;
  onInitialize?(): Promise<void>;
  onClosing?(context: ZLinkSpotClosingContext, cleanupSignal: AbortSignal): Promise<void>;
  onCreateActor?(actor: ZLinkActorMembership, createRequest: ZLinkMessage): Promise<void>;
}
