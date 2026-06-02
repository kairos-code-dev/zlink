const { createChannelClient, startChannelServer } = require('../../../../shared/channel-runtime');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { MatchBingoHandler } = require('./Handlers/match-bingo-handler');

async function buildApiServerHost(options) {
  const playClient = await createChannelClient({
    channelName: 'bingo.play',
    peers: [options.playEndpoint]
  });
  const authenticatePlayer = new AuthenticatePlayerHandler();
  const matchBingo = new MatchBingoHandler(playClient);

  await startChannelServer({
    endpoint: options.apiEndpoint,
    channelName: 'bingo.api',
    handlerGroups: ['api'],
    beforeReady: () => playClient.request('Ping', {}, 1000),
    handlers: [
      { group: 'api', packetName: 'AuthenticatePlayerReq', handle: (request) => authenticatePlayer.handle(request) },
      { group: 'api', packetName: 'MatchBingoApiReq', handle: (request) => matchBingo.handle(request) }
    ]
  });
}

export { buildApiServerHost };
