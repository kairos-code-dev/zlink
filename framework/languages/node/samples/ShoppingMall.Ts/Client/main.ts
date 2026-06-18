import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { loadSampleConfig } from './Configuration/sample-config';
import { ShoppingMallClientScenario } from './shoppingmall-client-scenario';
import { createShoppingMallClientModule } from './shoppingmall-client-module';
import type { ZLinkChannelClient } from '@zlink-systems/framework';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const ShoppingMallClientModule = createShoppingMallClientModule(config);
  const app = await NestFactory.createApplicationContext(ShoppingMallClientModule, {
    logger: false,
    abortOnError: false
  });
  try {
    const channels = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
    await new ShoppingMallClientScenario().run(channels);
  } finally {
    await app.close();
  }
  console.log('PASS ShoppingMall.Ts');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
