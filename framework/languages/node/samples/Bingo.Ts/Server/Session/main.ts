require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_CHANNEL_CLIENT, zlinkFramework, zlinkHandlers } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { AuthenticateSessionHandler } = require('./Sessions/Handlers/authenticate-session-handler');
const { BingoSession } = require('./Sessions/bingo-session');
const { SampleNames, SampleTimings } = require('../../Shared/Configuration/sample-names');
const { PacketNames, withPlayerIdentity } = require('../../Shared/Contracts/messages');
const { SESSION_CONTEXTS } = require('./session-tokens');

type ProviderAccessor = {
  get(token: unknown): any;
};

type RouteContext = {
  sourceNodeRid?: string;
  sourcePeerRid?: string;
};

type SessionContext = {
  actorId: string | null;
  displayName: string | null;
  actors: {
    bound: any[];
    bind(actor: any): Promise<void>;
  };
};

type RouteHandler = (request: any, context: RouteContext) => Promise<unknown> | unknown;

async function bootstrap(): Promise<void> {
  let app;
  const providers = {
    get(token: unknown): any {
      if (app === undefined) {
        throw new Error(`NestJS provider is not ready: ${String(token)}`);
      }
      return app.get(token, { strict: false });
    }
  };

  class BingoSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRoot(
        zlinkFramework()
          .clientServerChannel('bingo.api', (channel) => channel
            .client(process.env.BINGO_API_ENDPOINT))
          .clientServerChannel('bingo.play', (channel) => channel
            .client(process.env.BINGO_PLAY_ENDPOINT))
          .routerMesh('sample-route', (mesh) => mesh
            .bind(process.env.BINGO_SESSION_ENDPOINT)
            .routingId('session-server')
            .connect([])
            .handlerGroup('session'))
          .build()
      )
    ],
    providers: [
      { provide: SESSION_CONTEXTS, useValue: new Map() },
      AuthenticateSessionHandler,
      ...zlinkHandlers('session')
        .request(routeHandlerProvider(async (request, routeContext) => {
          const session = createSessionContext();
          const response = await providers.get(AuthenticateSessionHandler).handle(request, session);
          providers.get(SESSION_CONTEXTS).set(sessionKey(routeContext), session);
          return response;
        }), PacketNames.authenticateReq)
        .request(
          routeHandlerProvider((request, routeContext) => relayToPlay(providers, routeContext, PacketNames.matchBingoReq, request)),
          PacketNames.matchBingoReq
        )
        .request(
          routeHandlerProvider((request, routeContext) => relayToPlay(providers, routeContext, PacketNames.startBingoGameReq, request)),
          PacketNames.startBingoGameReq
        )
        .request(
          routeHandlerProvider((request, routeContext) => relayToPlay(providers, routeContext, PacketNames.bingoNotificationsReq, request)),
          PacketNames.bingoNotificationsReq
        )
        .request(routeHandlerProvider(() => ({ role: 'session-server', session: BingoSession.name })), PacketNames.ping)
        .providers()
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

function routeHandlerProvider(handle: RouteHandler): { provider: { provide: symbol; useValue: { handle(request: any, context: RouteContext): Promise<unknown> } } } {
  return {
    provider: {
      provide: Symbol('route.handler'),
      useValue: {
        async handle(request: any, context: RouteContext): Promise<unknown> {
          return await handle(request, context);
        }
      }
    }
  };
}

function createSessionContext(): SessionContext {
  return {
    actorId: null,
    displayName: null,
    actors: {
      bound: [],
      async bind(actor: any): Promise<void> {
        this.bound.push(actor);
      }
    }
  };
}

async function relayToPlay(
  providers: ProviderAccessor,
  routeContext: RouteContext,
  packetName: string,
  request: object
): Promise<unknown> {
  const zlinkClient = providers.get(ZLINK_CHANNEL_CLIENT);
  const session = requireSessionContext(providers.get(SESSION_CONTEXTS), routeContext);
  const actor = session.actors.bound[0];
  if (actor === undefined || session.actorId === null || session.displayName === null) {
    throw new Error(`Client must authenticate before relaying packet '${packetName}'.`);
  }
  const payload = withPlayerIdentity(request, session.actorId, session.displayName);
  return await zlinkClient
    .requestToChannel(SampleNames.playChannel, payload)
    .packetName(packetName)
    .timeout(SampleTimings.requestTimeout)
    .submit();
}

function requireSessionContext(sessionContexts: Map<string, SessionContext>, routeContext: RouteContext): SessionContext {
  const session = sessionContexts.get(sessionKey(routeContext));
  if (session === undefined) {
    throw new Error('Client must authenticate before relaying Bingo packets.');
  }
  return session;
}

function sessionKey(routeContext: RouteContext): string {
  return String(routeContext.sourceNodeRid ?? routeContext.sourcePeerRid ?? 'unknown');
}

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
