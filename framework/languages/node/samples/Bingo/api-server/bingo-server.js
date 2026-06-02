const nestjs = require('../../../packages/nestjs/dist');
const { BingoRoomSpot } = require('../play-server/bingo-room');
const { SampleBoundSessionRuntime } = require('../../shared/bound-session-runtime');

function createBingoServer() {
  const boundSessions = new SampleBoundSessionRuntime();
  const module = nestjs.ZLinkModule.forRoot({
    spotNodes: ['bingo-room'],
    spotFactories: [BingoRoomSpot]
  });
  const spots = getProvider(module, nestjs.ZLINK_SPOT_MANAGER);

  return {
    boundSessions,
    spots,
    BingoRoomSpot,
    boundSessionFor(actorId) {
      return boundSessions.createBoundSession(actorId);
    },
    bindSession(actorId, sessionId, generation) {
      return boundSessions.bind({
        nodeRid: 'bingo-session-node',
        actorId,
        generation: BigInt(generation)
      }, sessionId);
    }
  };
}

function getProvider(module, token) {
  const provider = module.providers.find((entry) => entry.provide === token);
  if (provider === undefined || provider.useValue === undefined) {
    throw new Error(`Sample provider is not registered: ${String(token)}`);
  }
  return provider.useValue;
}

module.exports = { createBingoServer };
