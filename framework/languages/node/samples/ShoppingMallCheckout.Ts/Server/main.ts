import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { loadSampleConfig } from './Configuration/sample-config';
import { createShoppingMallCheckoutModule } from './CheckoutWorkflow/shoppingmall-checkout-module';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const ShoppingMallCheckoutModule = createShoppingMallCheckoutModule(config);
  const app = await NestFactory.createApplicationContext(ShoppingMallCheckoutModule, {
    logger: false,
    abortOnError: false
  });
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.checkoutEndpoint
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
