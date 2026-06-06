require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT, zlinkHandlerGroup } = require('../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../../../shared/runtime-common');
const { AuthenticateSessionHandler } = require('./Sessions/Handlers/authenticate-session-handler');
const { BingoSession } = require('./Sessions/bingo-session');
const { SampleNames, SampleTimings } = require('../../Shared/Configuration/sample-names');
const { SESSION_CONTEXTS } = require('./session-tokens');

async function bootstrap() {
  let app;
  const providers = {
    get(token) {
      if (app === undefined) {
        throw new Error(`NestJS provider is not ready: ${String(token)}`);
      }
      return app.get(token, { strict: false });
    }
  };

  class BingoSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        clientServerChannels: {
          'bingo.api': {
            client: { manualConnections: [process.env.BINGO_API_ENDPOINT] }
          },
          'bingo.play': {
            client: { manualConnections: [process.env.BINGO_PLAY_ENDPOINT] }
          }
        },
        routerMeshes: {
          'sample-route': {
            bind: process.env.BINGO_SESSION_ENDPOINT,
            routingId: 'session-server',
            manualConnections: [],
            handlerGroups: ['session']
          }
        }
      })
    ],
    providers: [
      { provide: SESSION_CONTEXTS, useValue: new Map() },
      AuthenticateSessionHandler,
      ...zlinkHandlerGroup('session', [
        routeHandlerProvider('AuthenticateReq', async (request, routeContext) => {
          const session = createSessionContext();
          const response = await providers.get(AuthenticateSessionHandler).handle(request, session);
          providers.get(SESSION_CONTEXTS).set(sessionKey(routeContext), session);
          return response;
        }),
        routeHandlerProvider(
          'MatchBingoReq',
          (request, routeContext) => relayToPlay(providers, routeContext, 'MatchBingoReq', request)
        ),
        routeHandlerProvider(
          'StartBingoGameReq',
          (request, routeContext) => relayToPlay(providers, routeContext, 'StartBingoGameReq', request)
        ),
        routeHandlerProvider(
          'BingoNotificationsReq',
          (request, routeContext) => relayToPlay(providers, routeContext, 'BingoNotificationsReq', request)
        ),
        routeHandlerProvider('Ping', () => ({ role: 'session-server', session: BingoSession.name }))
      ])
    ]
  })(BingoSessionModule);

  app = await NestFactory.createApplicationContext(BingoSessionModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.BINGO_SESSION_ENDPOINT,
    routingId: 'session-server'
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await closeNestRuntime(app);
  }
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

async function relayToPlay(providers, routeContext, packetName, request) {
  const zlinkClient = providers.get(ZLINK_CHANNEL_CLIENT);
  const session = requireSessionContext(providers.get(SESSION_CONTEXTS), routeContext);
  const actor = session.actors.bound[0];
  if (actor === undefined || session.actorId === null || session.displayName === null) {
    throw new Error(`Client must authenticate before relaying packet '${packetName}'.`);
  }
  const payload = {
    ...request,
    actorId: session.actorId,
    displayName: session.displayName
  };
  return await zlinkClient
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

bootstrap().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
