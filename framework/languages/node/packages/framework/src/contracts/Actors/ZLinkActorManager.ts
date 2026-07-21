import type { ActorRef } from '../Common';

export interface ZLinkActorManager {
  create(meshName: string, actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
  create(
    meshName: string,
    actorId: string,
    actorType: string,
    createRequest: unknown,
    signal?: AbortSignal
  ): Promise<ActorRef>;
  find(meshName: string, actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
  getOrCreate(meshName: string, actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
  getOrCreate(
    meshName: string,
    actorId: string,
    actorType: string,
    createRequest: unknown,
    signal?: AbortSignal
  ): Promise<ActorRef>;
}
