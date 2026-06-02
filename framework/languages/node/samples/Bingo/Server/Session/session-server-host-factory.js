const { createChannelClient } = require('../../../shared/channel-runtime');
const { startRouteServer } = require('../../../shared/route-runtime');
const { AuthenticateSessionHandler } = require('./Sessions/Handlers/authenticate-session-handler');
const { BingoSession } = require('./Sessions/bingo-session');

async function buildSessionServerHost(options) {
  const apiClient = await createChannelClient({
    channelName: 'bingo.api',
    peers: [options.apiEndpoint].filter(Boolean)
  });
  const playClient = await createChannelClient({
    channelName: 'bingo.play',
    peers: [options.playEndpoint].filter(Boolean)
  });
  const authenticateSession = new AuthenticateSessionHandler(apiClient, playClient);

  return await startRouteServer({
    endpoint: options.sessionEndpoint,
    routingId: 'session-server',
    handlers: [
      { packetName: 'AuthenticateReq', handle: (request, context) => authenticateSession.handle(request, fakeSessionContext(context)) },
      { packetName: 'Ping', handle: () => ({ role: 'session-server', session: BingoSession.name }) }
    ]
  });
}

module.exports = { buildSessionServerHost };

function fakeSessionContext() {
  return {
    actors: {
      bound: [],
      async bind(actor) {
        this.bound.push(actor);
      }
    }
  };
}
