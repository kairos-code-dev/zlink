import type { ActorRef, RoutingId } from '../Common';
import type { ZLinkActorJoinEntrySpotCall, ZLinkActorJoinSpotCall } from './ZLinkActorFactory';

export interface ZLinkActorManager {
  create(actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
  create(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
  find(actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
  getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
  getOrCreate(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
}

export interface ZLinkActorGateway {
  joinSpot(actorRef: ActorRef, spotRid: RoutingId, request?: unknown): ZLinkActorJoinSpotCall;
  joinEntrySpot(actorRef: ActorRef, nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall;
}
