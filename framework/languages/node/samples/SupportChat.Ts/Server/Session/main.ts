import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { createSupportChatSessionModule } from './supportchat-session-module';
import { SampleNames } from '../Configuration/sample-names';
import { loadSampleConfig } from '../Configuration/sample-config';

async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const SupportChatSessionModule = createSupportChatSessionModule(config);
  const app = await NestFactory.createApplicationContext(SupportChatSessionModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.sessionEndpoint,
    stream: SampleNames.sessionStream
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await closeNestRuntime(app);
  }
}

bootstrap().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
