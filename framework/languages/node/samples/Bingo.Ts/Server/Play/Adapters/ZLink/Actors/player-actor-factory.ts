const { PlayerActor } = require('./player-actor');
import type { PlayerActor as PlayerActorType } from './player-actor';
import type {
  ZLinkActorContext,
  ZLinkActorFactory
} from '../../../../../../../packages/framework/dist';
import type { BingoActorRef } from '../../../../../Shared/Contracts/messages';

class PlayerActorFactory implements ZLinkActorFactory {
  private readonly actors: Map<string, PlayerActorType>;

  constructor() {
    this.actors = new Map();
  }

  create(actorId: string, context: ZLinkActorContext): PlayerActorType {
    const actorRef: BingoActorRef = {
      nodeRid: context.spotRid ?? 'bingo.room.node',
      actorId,
      generation: this.actors.size + 1
    };
    const actor = new PlayerActor(actorId, actorId, actorRef);
    Object.defineProperty(actor, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });
    this.actors.set(actorId, actor);
    return actor;
  }

  async ensure(actorId: string, displayName: string): Promise<PlayerActorType> {
    if (!this.actors.has(actorId)) {
      const actorRef: BingoActorRef = {
        nodeRid: 'bingo.room.node',
        actorId,
        generation: this.actors.size + 1
      };
      this.actors.set(actorId, new PlayerActor(
        actorId,
        displayName,
        actorRef
      ));
    }
    const actor = this.actors.get(actorId);
    if (actor === undefined) {
      throw new Error(`Actor '${actorId}' was not created.`);
    }
    return actor;
  }
}

export { PlayerActorFactory };
