const { PlayerActor } = require('./player-actor');
const { Inject } = require('@nestjs/common');
const { SampleBoundSessionRuntime } = require('../bound-session-runtime');

class PlayerActorFactory {
  [key: string]: any;
  constructor(boundSessions) {
    this.boundSessions = boundSessions;
    this.actors = new Map();
    this.sessionSeq = 0;
  }

  async ensure(actorId, displayName) {
    if (!this.actors.has(actorId)) {
      this.sessionSeq += 1;
      await this.boundSessions.bind({
        nodeRid: 'bingo.room.node',
        actorId,
        generation: BigInt(this.sessionSeq)
      }, `session-${actorId}`);
      this.actors.set(actorId, new PlayerActor(
        actorId,
        displayName,
        this.boundSessions.createBoundSession(actorId)
      ));
    }
    return this.actors.get(actorId);
  }
}

Inject(SampleBoundSessionRuntime)(PlayerActorFactory, undefined, 0);

export { PlayerActorFactory };
