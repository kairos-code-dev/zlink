import type { ActorRef } from '../Common';
import type { ZLinkActor } from './ZLinkActor';
import type { ZLinkActorContext } from './ZLinkActorContext';

export interface ZLinkActorJoinResult<TReply = unknown> {
  readonly accepted: boolean;
  readonly actor?: ActorRef;
  readonly reply?: TReply;
}

export interface ZLinkActorJoinCall<TSelf> {
  timeout(timeoutMs: number): TSelf;
  submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorYieldJoinCall<TSelf> extends ZLinkActorJoinCall<TSelf> {
  yield<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinSpotCall extends ZLinkActorYieldJoinCall<ZLinkActorJoinSpotCall> {}

export interface ZLinkActorJoinEntrySpotCall extends ZLinkActorYieldJoinCall<ZLinkActorJoinEntrySpotCall> {}

export interface ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext, signal?: AbortSignal): Promise<ZLinkActor> | ZLinkActor;
}
