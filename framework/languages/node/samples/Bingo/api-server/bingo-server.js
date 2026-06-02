const framework = require('../../../packages/framework/dist');
const { BingoRoomSpot } = require('../play-server/bingo-room');
const { BoundNotificationHub } = require('../session-server/notifications');

function createBingoServer() {
  const notificationHub = new BoundNotificationHub();
  const spots = new framework.DefaultZLinkSpotManager({
    spotFactories: [BingoRoomSpot]
  });

  return {
    notificationHub,
    spots,
    BingoRoomSpot,
    sessionFor(actorId) {
      return notificationHub.sessionFor(actorId);
    }
  };
}

module.exports = { createBingoServer };
