const { createChannelClient, startChannelServer } = require('../../../shared/channel-runtime');
const { AuthenticatePlayerHandler } = require('./Handlers/authenticate-player-handler');
const { MatchBingoHandler } = require('./Handlers/match-bingo-handler');

const PLAY_CLIENT = Symbol('bingo.play.client');

async function buildApiServerHost(options) {
  const playClient = await createChannelClient({
    channelName: 'bingo.play',
    peers: [options.playEndpoint]
  });

  await startChannelServer({
    endpoint: options.apiEndpoint,
    channelName: 'bingo.api',
    handlerGroups: ['api'],
    providers: [
      { provide: PLAY_CLIENT, useValue: playClient },
      AuthenticatePlayerHandler,
      {
        provide: MatchBingoHandler,
        inject: [PLAY_CLIENT],
        useFactory: (client) => new MatchBingoHandler(client)
      }
    ],
    handlers: []
  });
}

module.exports = { buildApiServerHost };
