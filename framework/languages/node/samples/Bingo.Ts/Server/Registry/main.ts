require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkHandlerGroup } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../../../../shared/runtime-common');

class BingoRegistryModule {}

Module({
  imports: [
    ZLinkModule.forRoot({
      routerMeshes: {
        'sample-route': {
          bind: process.env.BINGO_REGISTRY_ENDPOINT,
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
})(BingoRegistryModule);

async function bootstrap() {
  const app = await NestFactory.createApplicationContext(BingoRegistryModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: process.env.BINGO_REGISTRY_ENDPOINT,
    routingId: 'registry-server'
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await closeNestRuntime(app);
  }
}

bootstrap().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
