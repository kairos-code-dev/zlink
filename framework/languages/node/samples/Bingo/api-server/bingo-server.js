const nestjs = require('../../../packages/nestjs/dist');
const { BingoRoomSpot } = require('../play-server/bingo-room');
const { BoundNotificationHub } = require('../session-server/notifications');

function createBingoServer() {
  const notificationHub = new BoundNotificationHub();
  const module = nestjs.ZLinkModule.forRoot({
    spotNodes: ['bingo-room'],
    spotFactories: [BingoRoomSpot]
  });
  const spots = getProvider(module, nestjs.ZLINK_SPOT_MANAGER);

  return {
    notificationHub,
    spots,
    BingoRoomSpot,
    sessionFor(actorId) {
      return notificationHub.sessionFor(actorId);
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
