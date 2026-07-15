import { PlayerActor } from './player-actor';
import type { ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';

class PlayerActorFactory implements ZLinkActorFactory {
  async create(actorId: string, context: ZLinkActorContext): Promise<PlayerActor> {
    const actor = new PlayerActor(actorId);
    Object.defineProperty(actor, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });
    return actor;
  }
}

export { PlayerActorFactory };
