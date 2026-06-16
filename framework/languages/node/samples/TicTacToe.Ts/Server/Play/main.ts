import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { loadSampleConfig } from '../Configuration/sample-config';
import { createTicTacToePlayModule } from './tictactoe-play-module';
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
    spotEndpoint: config.playSpotEndpoint,
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
