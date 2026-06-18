import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { loadSampleConfig } from './Configuration/sample-config';
import { ShoppingMallCheckoutClientScenario } from './shoppingmall-checkout-client-scenario';
import { createShoppingMallCheckoutClientModule } from './shoppingmall-checkout-client-module';
import type { ZLinkChannelClient } from '@zlink-systems/framework';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const ShoppingMallCheckoutClientModule = createShoppingMallCheckoutClientModule(config);
  const app = await NestFactory.createApplicationContext(ShoppingMallCheckoutClientModule, {
    logger: false,
    abortOnError: false
  });
  try {
    const channels = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
    await new ShoppingMallCheckoutClientScenario().run(channels);
  } finally {
    await app.close();
  }
  console.log('PASS ShoppingMallCheckout.Ts');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
