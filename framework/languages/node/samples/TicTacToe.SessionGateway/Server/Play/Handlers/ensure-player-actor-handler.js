const { Inject } = require('@nestjs/common');
const { SampleBoundSessionRuntime } = require('../../../../shared/bound-session-runtime');
const { SampleNames } = require('../../../Shared/Configuration/sample-names');
const { SessionGatewayActorManager } = require('../session-gateway-actor-manager');

class EnsurePlayerActorHandler {
  constructor(actorManager, boundSessions) {
    this.actorManager = actorManager;
    this.boundSessions = boundSessions;
    this.generation = 0;
  }

  async handle(request) {
    this.generation += 1;
    const binding = await this.boundSessions.bind({
      nodeRid: 'session-gateway-play',
      actorId: request.actorId,
      generation: BigInt(this.generation)
    }, request.sessionId);
    const actor = await this.actorManager.getOrCreate(request.actorId, SampleNames.playerActorType);
    return {
      actorId: actor.actorId,
      sessionId: binding.sessionId,
      generation: this.generation
    };
  }
}

Inject(SessionGatewayActorManager)(EnsurePlayerActorHandler, undefined, 0);
Inject(SampleBoundSessionRuntime)(EnsurePlayerActorHandler, undefined, 1);

module.exports = { EnsurePlayerActorHandler };
