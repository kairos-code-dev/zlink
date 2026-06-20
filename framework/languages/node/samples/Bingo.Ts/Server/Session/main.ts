import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { createBingoSessionModule } from './bingo-session-module';
import { BingoSession } from './Sessions/bingo-session';
import { loadSampleConfig } from '../Configuration/sample-config';

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const BingoSessionModule = createBingoSessionModule(config);
  const app = await NestFactory.createApplicationContext(BingoSessionModule, {
    logger: false,
    abortOnError: false
  });

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
