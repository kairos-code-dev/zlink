import type { ZLinkActor } from '../Actors';
import type { ZLinkMessage } from '../Common';
import type { ZLinkActorMembership } from '../RouteMesh';
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
export interface ZLinkActorCreateResponse extends ZLinkSpotAcceptRejectResponse {}

export enum ZLinkSpotCloseReason {
  ExplicitClose = 0,
  HostShutdown = 1,
  RelocationOut = 2
}

export interface ZLinkSpotClosingContext {
  readonly reason: ZLinkSpotCloseReason;
  readonly deadline: Date;
}

export interface ZLinkSpotActorMembershipLifecycle {
  onJoinedActor(actor: ZLinkActorMembership): Promise<void>;
  onLeaveActor(actor: ZLinkActorMembership): Promise<void>;
  onDisconnectActor(actor: ZLinkActorMembership): Promise<void>;
}

export interface ZLinkUserSpotActorLifecycle extends ZLinkSpotActorMembershipLifecycle {
  // Admission observes the joining Actor by identity only. The framework keeps
  // fencing state such as the expected membership epoch inside the runtime so an
  // application callback never has to reason about it.
  onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse>;
}

export interface ZLinkSpot<TActor extends ZLinkActor = ZLinkActor> extends ZLinkUserSpotActorLifecycle {
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

export interface ZLinkEntrySpot<TActor extends ZLinkActor = ZLinkActor>
  extends ZLinkSpotActorMembershipLifecycle {
  readonly context: ZLinkEntrySpotContext<TActor>;
  configure?(): void;
  onInitialize?(): Promise<void>;
  onClosing?(context: ZLinkSpotClosingContext, cleanupSignal: AbortSignal): Promise<void>;
  onCreateActor?(
    actor: ZLinkActorMembership,
    createRequest: ZLinkMessage
  ): Promise<ZLinkActorCreateResponse>;
  onActorRelocated?(actor: ZLinkActorMembership): Promise<void>;
}
