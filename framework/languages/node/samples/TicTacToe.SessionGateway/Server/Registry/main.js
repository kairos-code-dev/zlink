require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkHandlerGroup } = require('../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../../../shared/runtime-common');

async function main() {
  class TicTacToeSessionGatewayRegistryModule {}

  Module({
    imports: [
      ZLinkModule.forRoot({
        routerMeshes: {
          'sample-route': {
            bind: process.env.TICTACTOE_SG_REGISTRY_ENDPOINT,
            routingId: 'registry-server',
            manualConnections: [],
            handlerGroups: ['registry']
          }
        }
      })
    ],
    providers: [
      ...zlinkHandlerGroup('registry', [{
        provider: { provide: Symbol('registry.ping'), useValue: { handle: () => ({ role: 'registry-server' }) } },
        packetName: 'Ping'
      }])
    ]
  })(TicTacToeSessionGatewayRegistryModule);

  const app = await NestFactory.createApplicationContext(TicTacToeSessionGatewayRegistryModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.TICTACTOE_SG_REGISTRY_ENDPOINT,
    routingId: 'registry-server'
  })}\n`);

  await waitForShutdown({ keepAlive: true });
  await closeNestRuntime(app);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
