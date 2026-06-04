const { createRouteClient, startRouteServer } = require('../../../shared/route-runtime');
const { SampleNames, SampleTimings } = require('../../Shared/Configuration/sample-names');
const { PacketNames } = require('../../Shared/Contracts/messages');
const { AuthenticateSessionPacketHandler } = require('./Sessions/Handlers/authenticate-session-packet-handler');
const { CreateMatchSessionPacketHandler } = require('./Sessions/Handlers/create-match-session-packet-handler');
const { PlaceMarkSessionPacketHandler } = require('./Sessions/Handlers/place-mark-session-packet-handler');

async function main() {
  const apiClient = await createRouteClient({
    endpoint: process.env.TICTACTOE_SG_SESSION_API_CLIENT_ENDPOINT,
    routingId: 'session-api-client',
    peers: [process.env.TICTACTOE_SG_API_ENDPOINT]
  });
  const playClient = await createRouteClient({
    endpoint: process.env.TICTACTOE_SG_SESSION_PLAY_CLIENT_ENDPOINT,
    routingId: 'session-play-client',
    peers: [process.env.TICTACTOE_SG_PLAY_ENDPOINT]
  });
  const sessions = new Map();
  const authenticate = new AuthenticateSessionPacketHandler(apiClient, playClient);
  const createMatch = new CreateMatchSessionPacketHandler(apiClient, playClient);
  const placeMark = new PlaceMarkSessionPacketHandler(playClient);

  await startRouteServer({
    endpoint: process.env.TICTACTOE_SG_SESSION_ENDPOINT,
    routingId: SampleNames.sessionNode,
    handlers: [
      {
        packetName: PacketNames.authenticateSessionReq,
        handle: async (request, routeContext) => {
          const sessionId = sessionKey(routeContext);
          const result = await authenticate.handle({ ...request, sessionId });
          sessions.set(sessionId, result.session);
          return result.response;
        }
      },
      {
        packetName: PacketNames.createMatchSessionReq,
        handle: (request, routeContext) => createMatch.handle(request, requireSession(sessions, routeContext))
      },
      {
        packetName: PacketNames.joinMatchReq,
        handle: (request, routeContext) => playClient.request(
          SampleNames.playNode,
          PacketNames.joinMatchReq,
          { matchId: request.matchId, actorId: requireSession(sessions, routeContext).actorId },
          SampleTimings.requestTimeout
        )
      },
      {
        packetName: PacketNames.placeMarkSessionReq,
        handle: (request, routeContext) => placeMark.handle(request, requireSession(sessions, routeContext))
      },
      {
        packetName: PacketNames.notificationsReq,
        handle: (request, routeContext) => playClient.request(
          SampleNames.playNode,
          PacketNames.notificationsReq,
          { actorId: requireSession(sessions, routeContext).actorId, afterSeq: request.afterSeq ?? 0 },
          SampleTimings.requestTimeout
        )
      },
      { packetName: 'Ping', handle: () => ({ role: SampleNames.sessionNode }) }
    ]
  });
  await apiClient.stop();
  await playClient.stop();
}

function requireSession(sessions, routeContext) {
  const session = sessions.get(sessionKey(routeContext));
  if (session === undefined) {
    throw new Error('AuthenticateSessionReq is required before session gateway packets.');
  }
  return session;
}

function sessionKey(routeContext) {
  return String(routeContext.sourceNodeRid ?? routeContext.sourcePeerRid ?? 'unknown');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
