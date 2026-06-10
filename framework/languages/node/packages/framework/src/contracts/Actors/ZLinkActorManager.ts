import type { ZLinkActor } from './ZLinkActor';

export interface ZLinkActorManager {
  create(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor>;
  find(actorId: string, signal?: AbortSignal): Promise<ZLinkActor | undefined>;
  getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor>;
}
