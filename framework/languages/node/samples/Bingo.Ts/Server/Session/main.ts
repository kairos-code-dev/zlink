import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { createBingoSessionModule } from './bingo-session-module';
import { BingoSession } from './Sessions/bingo-session';
import { BINGO_SAMPLE_CONFIG } from '../Configuration/sample-config';
import type { BingoSampleConfig } from '../Configuration/sample-config';
import { SampleNames } from '../Configuration/sample-names';
import { reportBingoRoutingId } from '../Configuration/routing-id-report';

async function bootstrap(): Promise<void> {
  const BingoSessionModule = createBingoSessionModule();
  const app = await NestFactory.createApplicationContext(BingoSessionModule, {
    logger: false,
    abortOnError: false
  });
  const config = app.get<BingoSampleConfig>(BINGO_SAMPLE_CONFIG);
  await reportBingoRoutingId(app, 'session', 'bingo.session', [
    SampleNames.roomSpotNode
  ]);

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.sessionEndpoint,
    stream: BingoSession.name
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
