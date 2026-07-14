import type { ActorRef } from '../Common';
import type { ZLinkActor } from './ZLinkActor';
import type { ZLinkActorContext } from './ZLinkActorContext';

export type ZLinkActorJoinResult<TReply = unknown> =
  | { readonly status: 'accepted'; readonly actor: ActorRef; readonly reply: TReply }
  | { readonly status: 'rejected'; readonly reply: TReply };

export interface ZLinkActorJoinCall<TSelf> {
  timeout(timeoutMs: number): TSelf;
  submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
  yield<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>>;
}

export interface ZLinkActorJoinSpotCall extends ZLinkActorJoinCall<ZLinkActorJoinSpotCall> {}

export interface ZLinkActorJoinEntrySpotCall extends ZLinkActorJoinCall<ZLinkActorJoinEntrySpotCall> {}

export interface ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext): Promise<ZLinkActor>;
}
