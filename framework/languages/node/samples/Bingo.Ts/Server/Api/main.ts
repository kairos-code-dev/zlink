require('reflect-metadata');

const { NestFactory } = require('@nestjs/core');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { createRegistryClient } = require('../discovery-support');
const { createBingoApiModule } = require('./bingo-api-module');
const { SampleNames } = require('../Configuration/sample-names');
const { loadSampleConfig } = require('../Configuration/sample-config');

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const registry = await createRegistryClient(config.registryEndpoint);
  const play = await registry.resolve(SampleNames.playService);
  const BingoApiModule = createBingoApiModule(config, play.endpoint);
  const app = await NestFactory.createApplicationContext(BingoApiModule, {
    logger: false,
    abortOnError: false
  });
  await registry.register(SampleNames.apiService, config.apiEndpoint);

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.apiEndpoint,
    channelName: 'bingo.api'
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
