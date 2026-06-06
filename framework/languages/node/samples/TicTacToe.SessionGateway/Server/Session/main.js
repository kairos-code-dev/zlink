require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_ROUTE_CLIENT, zlinkHandlerGroup } = require('../../../../packages/nestjs/dist');
const { closeNestRuntime, retry, waitForShutdown } = require('../../../shared/runtime-common');
const { SampleNames, SampleTimings } = require('../../Shared/Configuration/sample-names');
const { PacketNames } = require('../../Shared/Contracts/messages');
const { AuthenticateSessionPacketHandler } = require('./Sessions/Handlers/authenticate-session-packet-handler');
const { CreateMatchSessionPacketHandler } = require('./Sessions/Handlers/create-match-session-packet-handler');
const { PlaceMarkSessionPacketHandler } = require('./Sessions/Handlers/place-mark-session-packet-handler');
const { API_ROUTE_CLIENT, PLAY_ROUTE_CLIENT } = require('./session-tokens');

async function main() {
  let apiRouteClient;
  let playRouteClient;
  const apiClient = createRouteClientFacade(() => apiRouteClient);
  const playClient = createRouteClientFacade(() => playRouteClient);
  const sessions = new Map();
  let app;
  const providers = {
    get(token) {
      if (app === undefined) {
        throw new Error(`NestJS provider is not ready: ${String(token)}`);
      }
      return app.get(token, { strict: false });
    }
  };

  class TicTacToeSessionGatewaySessionApiClientModule {}
  class TicTacToeSessionGatewaySessionPlayClientModule {}
  class TicTacToeSessionGatewaySessionModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        routerMeshes: {
          [SampleNames.routeChannel]: {
            bind: process.env.TICTACTOE_SG_SESSION_API_CLIENT_ENDPOINT,
            routingId: `${SampleNames.sessionNode}-api-client`,
            manualConnections: [process.env.TICTACTOE_SG_API_ENDPOINT]
          }
        }
      })
    ]
  })(TicTacToeSessionGatewaySessionApiClientModule);

  Module({
    imports: [
      ZLinkModule.forRoot({
        routerMeshes: {
          [SampleNames.routeChannel]: {
            bind: process.env.TICTACTOE_SG_SESSION_PLAY_CLIENT_ENDPOINT,
            routingId: `${SampleNames.sessionNode}-play-client`,
            manualConnections: [process.env.TICTACTOE_SG_PLAY_ENDPOINT]
          }
        }
      })
    ]
  })(TicTacToeSessionGatewaySessionPlayClientModule);

  Module({
    imports: [
      ZLinkModule.forRoot({
        routerMeshes: {
          [SampleNames.routeChannel]: {
            bind: process.env.TICTACTOE_SG_SESSION_ENDPOINT,
            routingId: SampleNames.sessionNode,
            manualConnections: [],
            handlerGroups: ['session']
          }
        }
      })
    ],
    providers: [
      { provide: API_ROUTE_CLIENT, useValue: apiClient },
      { provide: PLAY_ROUTE_CLIENT, useValue: playClient },
      AuthenticateSessionPacketHandler,
      CreateMatchSessionPacketHandler,
      PlaceMarkSessionPacketHandler,
      ...zlinkHandlerGroup('session', [
        routeHandlerProvider(PacketNames.authenticateSessionReq, async (request, routeContext) => {
          const sessionId = sessionKey(routeContext);
          const result = await providers.get(AuthenticateSessionPacketHandler).handle({ ...request, sessionId });
          sessions.set(sessionId, result.session);
          return result.response;
        }),
        routeHandlerProvider(
          PacketNames.createMatchSessionReq,
          (request, routeContext) =>
            providers.get(CreateMatchSessionPacketHandler).handle(request, requireSession(sessions, routeContext))
        ),
        routeHandlerProvider(
          PacketNames.joinMatchReq,
          (request, routeContext) => playClient.request(
            SampleNames.playNode,
            PacketNames.joinMatchReq,
            { matchId: request.matchId, actorId: requireSession(sessions, routeContext).actorId },
            SampleTimings.requestTimeout
          )
        ),
        routeHandlerProvider(
          PacketNames.placeMarkSessionReq,
          (request, routeContext) =>
            providers.get(PlaceMarkSessionPacketHandler).handle(request, requireSession(sessions, routeContext))
        ),
        routeHandlerProvider(
          PacketNames.notificationsReq,
          (request, routeContext) => playClient.request(
            SampleNames.playNode,
            PacketNames.notificationsReq,
            { actorId: requireSession(sessions, routeContext).actorId, afterSeq: request.afterSeq ?? 0 },
            SampleTimings.requestTimeout
          )
        ),
        routeHandlerProvider('Ping', () => ({ role: SampleNames.sessionNode }))
      ])
    ]
  })(TicTacToeSessionGatewaySessionModule);

  const apiClientApp = await NestFactory.createApplicationContext(TicTacToeSessionGatewaySessionApiClientModule, {
    logger: false,
    abortOnError: false
  });
  apiRouteClient = apiClientApp.get(ZLINK_ROUTE_CLIENT, { strict: false });
  const playClientApp = await NestFactory.createApplicationContext(TicTacToeSessionGatewaySessionPlayClientModule, {
    logger: false,
    abortOnError: false
  });
  playRouteClient = playClientApp.get(ZLINK_ROUTE_CLIENT, { strict: false });
  app = await NestFactory.createApplicationContext(TicTacToeSessionGatewaySessionModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.TICTACTOE_SG_SESSION_ENDPOINT,
    routingId: SampleNames.sessionNode
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await closeNestRuntime(app);
    await closeNestRuntime(playClientApp);
    await closeNestRuntime(apiClientApp);
  }
}

function createRouteClientFacade(getClient) {
  return {
    async request(targetNodeRid, packetName, payload, timeoutMs = 1000) {
      return await retry(() => getClient()
        .request(SampleNames.routeChannel, targetNodeRid, payload)
        .packetName(packetName)
        .timeout(timeoutMs)
        .submit(), { maxAttempts: 100 });
    }
  };
}

function routeHandlerProvider(packetName, handle) {
  return {
    provider: {
      provide: Symbol(`route.${packetName}`),
      useValue: {
        async handle(request, context) {
          return await handle(request, context);
        }
      }
    },
    packetName,
  };
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
