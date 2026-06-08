require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const { ZLinkModule, zlinkFramework, zlinkHandlers } = require('../../../../../packages/nestjs/dist');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { PacketNames } = require('../../Shared/Contracts/messages');

class BingoRegistryModule {}

Module({
  imports: [
    ZLinkModule.forRoot(
      zlinkFramework()
        .routerMesh('sample-route', (mesh) => mesh
          .bind(process.env.BINGO_REGISTRY_ENDPOINT)
          .routingId('registry-server')
          .connect([])
          .handlerGroup('registry'))
        .build()
    )
  ],
  providers: [
    ...zlinkHandlers('registry')
      .request({ provider: { provide: Symbol('registry.ping'), useValue: { handle: () => ({ role: 'registry-server' }) } } }, PacketNames.ping)
      .providers()
  ]
})(BingoRegistryModule);

async function bootstrap(): Promise<void> {
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

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
