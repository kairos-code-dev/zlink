import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { createSupportChatApiModule } from './supportchat-api-module';
import { SampleNames } from '../Configuration/sample-names';
import { loadSampleConfig } from '../Configuration/sample-config';
async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const SupportChatApiModule = createSupportChatApiModule(config);
  const app = await NestFactory.createApplicationContext(SupportChatApiModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.apiEndpoint,
    channelName: SampleNames.apiChannel
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
