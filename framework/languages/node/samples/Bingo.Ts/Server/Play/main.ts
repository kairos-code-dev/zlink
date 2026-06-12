require('reflect-metadata');

const { NestFactory } = require('@nestjs/core');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { createRegistryClient } = require('../discovery-support');
const { createBingoPlayModule } = require('./bingo-play-module');
const { SampleNames } = require('../Configuration/sample-names');
const { loadSampleConfig } = require('../Configuration/sample-config');

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const BingoPlayModule = createBingoPlayModule(config);
  const app = await NestFactory.createApplicationContext(BingoPlayModule, {
    logger: false,
    abortOnError: false
  });
  const registry = await createRegistryClient(config.registryEndpoint);
  await registry.register(SampleNames.playService, config.playEndpoint);
  await registry.register(SampleNames.notificationService, config.notificationEndpoint);

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.playEndpoint,
    channelName: SampleNames.playChannel
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await registry.stop();
    await closeNestRuntime(app);
  }
}

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
