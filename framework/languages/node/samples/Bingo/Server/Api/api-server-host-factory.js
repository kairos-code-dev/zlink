const { createChannelClient } = require('../../../shared/channel-runtime');
const { startRouteServer } = require('../../../shared/route-runtime');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { MatchBingoHandler } = require('./Handlers/match-bingo-handler');

async function buildApiServerHost(options) {
  const playClient = await createChannelClient({
    channelName: 'bingo.play',
    peers: [options.playEndpoint]
  });
  const authenticatePlayer = new AuthenticatePlayerHandler();
  const matchBingo = new MatchBingoHandler(playClient);

  const server = await startRouteServer({
    endpoint: options.apiEndpoint,
    routingId: 'api-server',
    handlers: [
      { packetName: 'AuthenticatePlayer', handle: (request) => authenticatePlayer.handle(request) },
      { packetName: 'RunBingo', handle: (request) => matchBingo.handle(request) }
    ]
  });

  return {
    async stop() {
      await playClient.stop();
      await server?.stop?.();
    }
  };
}

module.exports = { buildApiServerHost };
