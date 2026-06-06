require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, ZLINK_ROUTE_CLIENT, zlinkHandlerGroup } = require('../../../../packages/nestjs/dist');
const { closeNestRuntime, retry, waitForShutdown } = require('../../../shared/runtime-common');
const { SampleNames, SampleTimings } = require('../../Shared/Configuration/sample-names');
const { PacketNames } = require('../../Shared/Contracts/messages');
const { AuthenticateActorHandler } = require('./Handlers/authenticate-actor-handler');
const { CreateMatchHandler } = require('./Handlers/create-match-handler');
const { PLAY_ROUTE_CLIENT } = require('./api-tokens');

async function main() {
  let routeClient;
  const playClient = createRouteClientFacade(() => routeClient);

  class TicTacToeSessionGatewayApiClientModule {}
  class TicTacToeSessionGatewayApiModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        routerMeshes: {
          [SampleNames.routeChannel]: {
            bind: process.env.TICTACTOE_SG_API_CLIENT_ENDPOINT,
            routingId: `${SampleNames.apiNode}-client`,
            manualConnections: [process.env.TICTACTOE_SG_PLAY_ENDPOINT]
          }
        }
      })
    ]
  })(TicTacToeSessionGatewayApiClientModule);

  Module({
    imports: [
      ZLinkModule.forRoot({
        routerMeshes: {
          [SampleNames.routeChannel]: {
            bind: process.env.TICTACTOE_SG_API_ENDPOINT,
            routingId: SampleNames.apiNode,
            manualConnections: [],
            handlerGroups: ['api']
          }
        }
      })
    ],
    providers: [
      { provide: PLAY_ROUTE_CLIENT, useValue: playClient },
      ...zlinkHandlerGroup('api', [
        [AuthenticateActorHandler, PacketNames.authenticateActorReq],
        [CreateMatchHandler, PacketNames.createMatchReq],
        routeHandlerProvider('Ping', () => ({ role: SampleNames.apiNode, timeout: SampleTimings.requestTimeout }))
      ])
    ]
  })(TicTacToeSessionGatewayApiModule);

  const clientApp = await NestFactory.createApplicationContext(TicTacToeSessionGatewayApiClientModule, {
    logger: false,
    abortOnError: false
  });
  routeClient = clientApp.get(ZLINK_ROUTE_CLIENT, { strict: false });
  const app = await NestFactory.createApplicationContext(TicTacToeSessionGatewayApiModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.TICTACTOE_SG_API_ENDPOINT,
    routingId: SampleNames.apiNode
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await closeNestRuntime(app);
    await closeNestRuntime(clientApp);
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

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
