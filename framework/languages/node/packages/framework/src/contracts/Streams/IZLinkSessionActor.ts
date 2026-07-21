import type { ActorRef, ZLinkMessage } from '../Common';
import type { ZLinkActor } from '../Actors';
import type { ZLinkSubmitResult } from '../RouteMesh';

export interface ZLinkSessionActors {
  readonly bound: readonly ZLinkSessionActor[];
  bind(actor: ZLinkActor | ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;
  bindOrGet(actor: ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor>;
  find(actorId: string): ZLinkSessionActor | undefined;
}

export interface ZLinkSessionActor {
  readonly actorId: string;
  readonly ref: ActorRef;
  relay(payload: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSubmitResult>;
  notifyDisconnected(signal?: AbortSignal): Promise<void>;
}
