const { PlayerActor } = require('./player-actor');

class PlayerActorFactory {
  [key: string]: any;
  constructor() {
    this.actors = new Map();
  }

  async ensure(actorId: string, displayName: string): Promise<any> {
    if (!this.actors.has(actorId)) {
      const actorRef = {
        nodeRid: 'bingo.room.node',
        actorId,
        generation: BigInt(this.actors.size + 1)
      };
      this.actors.set(actorId, new PlayerActor(
        actorId,
        displayName,
        actorRef
      ));
    }
    return this.actors.get(actorId);
  }
}

export { PlayerActorFactory };
