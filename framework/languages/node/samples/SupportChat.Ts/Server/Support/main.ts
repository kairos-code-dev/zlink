import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { closeNestRuntime, waitForShutdown } from '../runtime-support';
import { createSupportChatSupportModule } from './supportchat-support-module';
import { SampleNames } from '../Configuration/sample-names';
import { loadSampleConfig } from '../Configuration/sample-config';
async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const SupportChatSupportModule = createSupportChatSupportModule(config);
  const app = await NestFactory.createApplicationContext(SupportChatSupportModule, {
    logger: false,
    abortOnError: false
  });

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.supportEndpoint,
    channelName: SampleNames.supportChannel
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
