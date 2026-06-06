require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkHandlerGroup } = require('../../../../packages/nestjs/dist');
const { SampleBoundSessionRuntime } = require('../../../shared/bound-session-runtime');
const { closeNestRuntime, waitForShutdown } = require('../../../shared/runtime-common');
const { SampleNames } = require('../../Shared/Configuration/sample-names');
const { PacketNames } = require('../../Shared/Contracts/messages');
const { JoinMatchHandler } = require('./EntrySpot/Handlers/join-match-handler');
const { TicTacToeEntrySpot } = require('./EntrySpot/tictactoe-entry-spot');
const { PlaceMarkHandler } = require('./GameSpots/Handlers/place-mark-handler');
const { TicTacToeMatchDirectory } = require('./GameSpots/tictactoe-match-directory');
const { CreateMatchRoomHandler } = require('./Handlers/create-match-room-handler');
const { EnsurePlayerActorHandler } = require('./Handlers/ensure-player-actor-handler');
const { SessionGatewayActorManager } = require('./session-gateway-actor-manager');

async function main() {
  let app;
  const providers = {
    get(token) {
      if (app === undefined) {
        throw new Error(`NestJS provider is not ready: ${String(token)}`);
      }
      return app.get(token, { strict: false });
    }
  };

  class TicTacToeSessionGatewayPlayModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        routerMeshes: {
          'sample-route': {
            bind: process.env.TICTACTOE_SG_PLAY_ENDPOINT,
            routingId: SampleNames.playNode,
            manualConnections: [],
            handlerGroups: ['play']
          }
        }
      })
    ],
    providers: [
      SampleBoundSessionRuntime,
      SessionGatewayActorManager,
      TicTacToeMatchDirectory,
      EnsurePlayerActorHandler,
      CreateMatchRoomHandler,
      JoinMatchHandler,
      TicTacToeEntrySpot,
      PlaceMarkHandler,
      ...zlinkHandlerGroup('play', [
        routeHandlerProvider(
          PacketNames.ensurePlayerActorReq,
          (request) => providers.get(EnsurePlayerActorHandler).handle(request)
        ),
        routeHandlerProvider(
          PacketNames.createMatchReq,
          (request) => providers.get(CreateMatchRoomHandler).handle(request)
        ),
        routeHandlerProvider(
          PacketNames.joinMatchReq,
          (request) => providers.get(TicTacToeEntrySpot).handle(request)
        ),
        routeHandlerProvider(
          PacketNames.placeMarkReq,
          (request) => providers.get(PlaceMarkHandler).handle(request)
        ),
        routeHandlerProvider(
          PacketNames.notificationsReq,
          (request) => providers.get(SampleBoundSessionRuntime).deliveredFor(request.actorId, request.afterSeq)
        ),
        routeHandlerProvider('Ping', () => ({ role: SampleNames.playNode }))
      ])
    ]
  })(TicTacToeSessionGatewayPlayModule);

  app = await NestFactory.createApplicationContext(TicTacToeSessionGatewayPlayModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.TICTACTOE_SG_PLAY_ENDPOINT,
    routingId: SampleNames.playNode
  })}\n`);

  await waitForShutdown({ keepAlive: true });
  await closeNestRuntime(app);
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

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
