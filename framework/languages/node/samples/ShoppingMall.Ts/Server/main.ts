import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { loadSampleConfig } from './Configuration/sample-config';
import { createShoppingMallModule } from './OrderWorkflow/shoppingmall-workflow-module';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const ShoppingMallModule = createShoppingMallModule(config);
  const app = await NestFactory.createApplicationContext(ShoppingMallModule, {
    logger: false,
    abortOnError: false
  });
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.workflowEndpoint
  })}\n`);
  await waitForShutdown();
  await app.close();
}

function waitForShutdown(): Promise<void> {
  return new Promise<void>((resolve) => {
    process.once('SIGINT', resolve);
    process.once('SIGTERM', resolve);
  });
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
