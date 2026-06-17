import type { ActorRef } from '../Common';
import type { ZLinkActor } from './ZLinkActor';
import type { ZLinkActorContext } from './ZLinkActorContext';

export interface ZLinkActorJoinResult<TReply = unknown> {
  readonly resultCode: number;
  readonly actor: ActorRef;
  readonly reply?: TReply;
}

export interface ZLinkActorJoinSpotCall {
  timeout(timeoutMs: number): this;
  submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinEntrySpotCall {
  timeout(timeoutMs: number): this;
  submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext, signal?: AbortSignal): Promise<ZLinkActor> | ZLinkActor;
}
