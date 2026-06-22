import { PlayerActor } from './player-actor';
import type { PlayerActor as PlayerActorType } from './player-actor';
import type {
  ZLinkActorContext,
  ZLinkActorFactory
} from '@zlink-systems/framework';

class PlayerActorFactory implements ZLinkActorFactory {
  private readonly actors: Map<string, PlayerActorType>;

  constructor() {
    this.actors = new Map();
  }

  create(actorId: string, context: ZLinkActorContext): PlayerActorType {
    const existing = this.actors.get(actorId);
    if (existing !== undefined) {
      Object.defineProperty(existing, 'context', {
        configurable: true,
        enumerable: false,
        value: context
      });
      return existing;
    }
    const actor = new PlayerActor(actorId, actorId);
    Object.defineProperty(actor, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });
    this.actors.set(actorId, actor);
    return actor;
  }

  get(actorId: string): PlayerActorType {
    const actor = this.actors.get(actorId);
    if (actor === undefined) {
      throw new Error(`Actor '${actorId}' was not created.`);
    }
    return actor;
  }
}

export { PlayerActorFactory };
