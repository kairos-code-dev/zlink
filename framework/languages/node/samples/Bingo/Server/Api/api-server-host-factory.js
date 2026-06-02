const { createRouteClient, startRouteServer } = require('../../../shared/route-runtime');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { MatchBingoHandler } = require('./Handlers/match-bingo-handler');

async function buildApiServerHost(options) {
  let playClient;
  const authenticatePlayer = new AuthenticatePlayerHandler();
  const matchBingo = new MatchBingoHandler(async () => {
    playClient ??= await createRouteClient({
      endpoint: options.apiClientEndpoint,
      routingId: 'api-client',
      peers: [options.playEndpoint]
    });
    return playClient;
  });

  const server = await startRouteServer({
    endpoint: options.apiEndpoint,
    routingId: 'api-server',
    peers: [options.playEndpoint],
    handlers: [
      { packetName: 'AuthenticatePlayer', handle: (request) => authenticatePlayer.handle(request) },
      { packetName: 'RunBingo', handle: (request) => matchBingo.handle(request) }
    ]
  });

  return {
    async stop() {
      await playClient?.stop();
      await server?.stop?.();
    }
  };
}

module.exports = { buildApiServerHost };
