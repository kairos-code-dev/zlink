const { startChannelServer } = require('../../../shared/channel-runtime');
const { AllocateBingoRoomHandler } = require('./Handlers/allocate-bingo-room-handler');

async function buildPlayServerHost(options) {
  const allocateBingoRoom = new AllocateBingoRoomHandler();
  return await startChannelServer({
    endpoint: options.playEndpoint,
    channelName: 'bingo.play',
    handlerGroups: ['play'],
    handlers: [
      { group: 'play', packetName: 'AllocateBingoRoom', handle: (request) => allocateBingoRoom.handle(request) },
      { group: 'play', packetName: 'Ping', handle: () => ({ ok: true }) }
    ]
  });
}

module.exports = { buildPlayServerHost };
