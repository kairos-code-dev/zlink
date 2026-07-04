import { SupportUserActor } from './support-user-actor';

class SupportActorDirectory {
  private readonly actors = new Map<string, SupportUserActor>();

  bind(actor: SupportUserActor): SupportUserActor {
    this.actors.set(actor.actorId, actor);
    return actor;
  }

  get(actorId: string): SupportUserActor | undefined {
    return this.actors.get(actorId);
  }
}

export { SupportActorDirectory };
