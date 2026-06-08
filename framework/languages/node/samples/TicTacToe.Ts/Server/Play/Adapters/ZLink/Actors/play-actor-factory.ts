const { PlayActor } = require('./play-actor');
const { actorDisplayName } = require('../../../../../Shared/Contracts/messages');
import type {
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

class PlayActorFactory {
  [key: string]: any;
  constructor() {
    this.actors = new Map();
  }

  ensure(actorId: string): TicTacToeActor & { notifications: any[]; session: unknown } {
    if (!this.actors.has(actorId)) {
      this.actors.set(actorId, new PlayActor(actorId, actorDisplayName(actorId)));
    }
    return this.actors.get(actorId);
  }
}

export { PlayActorFactory };
