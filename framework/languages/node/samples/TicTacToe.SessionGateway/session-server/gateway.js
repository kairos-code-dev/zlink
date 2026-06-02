const framework = require('../../../packages/framework/dist');
const nestjs = require('../../../packages/nestjs/dist');
const { SessionPlayerActorFactory } = require('../play-server/session-actor');
const { SampleBoundSessionRuntime } = require('../../shared/bound-session-runtime');

function createSessionGateway() {
  const boundSessions = new SampleBoundSessionRuntime();
  nestjs.ZLinkModule.forRoot({
    spotNodes: ['session-gateway'],
    actorFactories: { player: SessionPlayerActorFactory }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', SessionPlayerActorFactory]]),
    boundSessionFactory(actorId) {
      return boundSessions.createBoundSession(actorId);
    }
  });

  return {
    boundSessions,
    manager,
    async bind(actorId, sessionId, generation) {
      return await boundSessions.bind({
        nodeRid: 'session-node',
        actorId,
        generation: BigInt(generation)
      }, sessionId);
    },
    staleUnbind(actor) {
      boundSessions.unbind(actor);
    }
  };
}

module.exports = { createSessionGateway };
