const { startRouteServer } = require('../../../shared/route-runtime');
const { RunBingoRoomHandler } = require('./Handlers/run-bingo-room-handler');

async function buildPlayServerHost(options) {
  const runBingoRoom = new RunBingoRoomHandler();
  return await startRouteServer({
    endpoint: options.playEndpoint,
    routingId: 'play-server',
    handlers: [{ packetName: 'RunBingoRoom', handle: (request) => runBingoRoom.handle(request) }]
  });
}

module.exports = { buildPlayServerHost };
