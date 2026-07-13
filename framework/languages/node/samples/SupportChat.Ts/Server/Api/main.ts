import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { createSupportChatApiModule } from './supportchat-api-module';
import { loadSampleConfig } from '../Configuration/sample-config';
import { waitForShutdown } from '../runtime-support';

async function main(): Promise<void> {
  const app = await NestFactory.createApplicationContext(createSupportChatApiModule(loadSampleConfig()), {
    logger: false,
    abortOnError: false
  });
  process.stdout.write(`${JSON.stringify({ event: 'ready', role: 'api' })}\n`);
  await waitForShutdown();
  await app.close();
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
