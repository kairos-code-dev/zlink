import type { ActorRef, Message } from '../Common';
import type { ZLinkActor } from './ZLinkActor';
import type { ZLinkActorContext } from './ZLinkActorContext';

export interface ZLinkActorJoinResult {
  readonly resultCode: number;
  readonly actor: ActorRef;
  readonly reply?: Message;
}

export interface ZLinkActorJoinSpotCall {
  timeout(timeoutMs: number): this;
  submit(signal?: AbortSignal): Promise<ZLinkActorJoinResult>;
}

export interface ZLinkActorJoinEntrySpotCall {
  timeout(timeoutMs: number): this;
  submit(signal?: AbortSignal): Promise<ActorRef>;
}

export interface ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext, signal?: AbortSignal): Promise<ZLinkActor> | ZLinkActor;
}
