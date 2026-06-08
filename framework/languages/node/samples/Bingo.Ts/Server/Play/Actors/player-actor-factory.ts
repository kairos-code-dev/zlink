const { PlayerActor } = require('./player-actor');
const { Inject } = require('@nestjs/common');
const { SampleBoundSessionRuntime } = require('../bound-session-runtime');

type LocalBingoActorRef = {
  nodeRid: string;
  actorId: string;
  generation: bigint;
};

class PlayerActorFactory {
  [key: string]: any;
  constructor(boundSessions: any) {
    this.boundSessions = boundSessions;
    this.actors = new Map();
    this.sessionSeq = 0;
  }

  async ensure(actorId: string, displayName: string): Promise<any> {
    if (!this.actors.has(actorId)) {
      this.sessionSeq += 1;
      const actorRef: LocalBingoActorRef = {
        nodeRid: 'bingo.room.node',
        actorId,
        generation: BigInt(this.sessionSeq)
      };
      await this.boundSessions.bind(actorRef, `session-${actorId}`);
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
