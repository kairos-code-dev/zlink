import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ZLINK_DRAIN_CONTROL } from '@zlink-systems/nestjs';
import type { ZLinkDrainControl } from '@zlink-systems/framework';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { createBingoPlayModule } from './bingo-play-module';
import { SampleNames } from '../Configuration/sample-names';
import { BINGO_SAMPLE_CONFIG } from '../Configuration/sample-config';
import type { BingoSampleConfig } from '../Configuration/sample-config';
import { reportBingoRoutingId } from '../Configuration/routing-id-report';
async function bootstrap(): Promise<void> {
  const BingoPlayModule = createBingoPlayModule();
  const app = await NestFactory.createApplicationContext(BingoPlayModule, {
    logger: false,
    abortOnError: false
  });
  const config = app.get<BingoSampleConfig>(BINGO_SAMPLE_CONFIG);
  await reportBingoRoutingId(app, 'play', 'bingo.play', [
    SampleNames.playChannel,
    SampleNames.roomSpotNode
  ]);

  const drain = app.get<ZLinkDrainControl>(ZLINK_DRAIN_CONTROL);
  const shutdown = new AbortController();
  const beginDrain = () => {
    console.log('bingo-drain requested');
    void drain.drain().then((result) => {
      console.log(`bingo-drain result=${result.kind}`);
      process.removeListener('SIGUSR2', beginDrain);
      process.removeListener('SIGBREAK', beginDrain);
      shutdown.abort();
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
    await waitForShutdown({ keepAlive: true, signal: shutdown.signal });
  } finally {
    console.log('bingo-play runtime closing');
    await closeNestRuntime(app);
    console.log('bingo-play runtime closed');
  }
}

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
