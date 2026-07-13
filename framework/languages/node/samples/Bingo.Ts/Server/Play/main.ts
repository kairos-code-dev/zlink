import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ZLINK_DRAIN_CONTROL } from '@zlink-systems/nestjs';
import type { ZLinkDrainControl } from '@zlink-systems/framework';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { createBingoPlayModule } from './bingo-play-module';
import { SampleNames } from '../Configuration/sample-names';
import { loadSampleConfig } from '../Configuration/sample-config';
async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const BingoPlayModule = createBingoPlayModule(config);
  const app = await NestFactory.createApplicationContext(BingoPlayModule, {
    logger: false,
    abortOnError: false
  });

  const drain = app.get<ZLinkDrainControl>(ZLINK_DRAIN_CONTROL);
  const beginDrain = () => {
    console.log('bingo-drain requested');
    void drain.drain().then((result) => {
      console.log(`bingo-drain result=${result.kind}`);
    }).catch((error) => {
      console.error('bingo-drain failed', error);
      process.exitCode = 1;
    });
  };
  process.once('SIGUSR2', beginDrain);
  process.once('SIGBREAK', beginDrain);

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.playEndpoint,
    channelName: SampleNames.playChannel
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
