require('reflect-metadata');

const { NestFactory } = require('@nestjs/core');
const { closeNestRuntime, waitForShutdown } = require('../runtime-support');
const { loadSampleConfig } = require('../Configuration/sample-config');
const { createTicTacToePlayModule } = require('./tictactoe-play-module');

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const TicTacToePlayModule = createTicTacToePlayModule(config);
  const channelApp = await NestFactory.createApplicationContext(TicTacToePlayModule, {
    logger: false,
    abortOnError: false
  });
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.playEndpoint,
    streamEndpoint: config.playStreamEndpoint
  })}\n`);
  await waitForShutdown();
  await closeNestRuntime(channelApp);
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
