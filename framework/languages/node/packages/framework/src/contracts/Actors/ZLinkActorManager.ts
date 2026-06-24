import type { ActorRef } from '../Common';

export interface ZLinkActorManager {
  create(actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
  create(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
  find(actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
  getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
  getOrCreate(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
}
