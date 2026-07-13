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

  require(actorId: string): SupportUserActor {
    const actor = this.get(actorId);
    if (actor === undefined) {
      throw new Error(`Support actor '${actorId}' was not created.`);
    }
    return actor;
  }
}

export { SupportActorDirectory };
