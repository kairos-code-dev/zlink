const { PlayActor } = require('./play-actor');
const { actorDisplayName } = require('../../../Shared/Contracts/messages');

class PlayActorFactory {
  [key: string]: any;
  constructor() {
    this.actors = new Map();
  }

  ensure(actorId) {
    if (!this.actors.has(actorId)) {
      this.actors.set(actorId, new PlayActor(actorId, actorDisplayName(actorId)));
    }
    return this.actors.get(actorId);
  }
}

export { PlayActorFactory };
