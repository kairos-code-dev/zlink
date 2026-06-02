const { createChannelClient } = require('../../../../shared/channel-runtime');
const { startRouteServer } = require('../../../../shared/route-runtime');
const { AuthenticateSessionHandler } = require('./Sessions/Handlers/authenticate-session-handler');
const { BingoSession } = require('./Sessions/bingo-session');
const { SampleNames, SampleTimings } = require('../../Shared/Configuration/sample-names');

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
  const sessionContexts = new Map();

  return await startRouteServer({
    endpoint: options.sessionEndpoint,
    routingId: 'session-server',
    handlers: [
      {
        packetName: 'AuthenticateReq',
        handle: async (request, routeContext) => {
          const session = createSessionContext();
          const response = await authenticateSession.handle(request, session);
          sessionContexts.set(sessionKey(routeContext), session);
          return response;
        }
      },
      {
        packetName: 'MatchBingoReq',
        handle: (request, routeContext) => relayToPlay(playClient, requireSessionContext(sessionContexts, routeContext), 'MatchBingoReq', request)
      },
      {
        packetName: 'StartBingoGameReq',
        handle: (request, routeContext) => relayToPlay(playClient, requireSessionContext(sessionContexts, routeContext), 'StartBingoGameReq', request)
      },
      {
        packetName: 'BingoNotificationsReq',
        handle: (request, routeContext) => relayToPlay(playClient, requireSessionContext(sessionContexts, routeContext), 'BingoNotificationsReq', request)
      },
      { packetName: 'Ping', handle: () => ({ role: 'session-server', session: BingoSession.name }) }
    ]
  });
}

export { buildSessionServerHost };

function createSessionContext() {
  return {
    actorId: null,
    displayName: null,
    actors: {
      bound: [],
      async bind(actor) {
        this.bound.push(actor);
      }
    }
  };
}

async function relayToPlay(playClient, session, packetName, request) {
  const actor = session.actors.bound[0];
  if (actor === undefined || session.actorId === null || session.displayName === null) {
    throw new Error(`Client must authenticate before relaying packet '${packetName}'.`);
  }
  const payload = {
    ...request,
    actorId: session.actorId,
    displayName: session.displayName
  };
  return await playClient
    .requestToChannel(SampleNames.playChannel, payload)
    .packetName(packetName)
    .timeout(SampleTimings.requestTimeout)
    .submit();
}

function requireSessionContext(sessionContexts, routeContext) {
  const session = sessionContexts.get(sessionKey(routeContext));
  if (session === undefined) {
    throw new Error('Client must authenticate before relaying Bingo packets.');
  }
  return session;
}

function sessionKey(routeContext) {
  return String(routeContext.sourceNodeRid ?? routeContext.sourcePeerRid ?? 'unknown');
}
