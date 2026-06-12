require('reflect-metadata');

const { NestFactory } = require('@nestjs/core');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { SampleNames } = require('../Configuration/sample-names');
const { loadSampleConfig } = require('../Configuration/sample-config');
const { createBingoRegistryModule } = require('./bingo-registry-module');

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const BingoRegistryModule = createBingoRegistryModule(config);
  const app = await NestFactory.createApplicationContext(BingoRegistryModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.registryEndpoint,
    channelName: SampleNames.registryChannel
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
