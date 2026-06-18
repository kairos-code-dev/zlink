import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { loadSampleConfig } from './Configuration/sample-config';
import { DeliveryDispatchClientScenario } from './deliverydispatch-client-scenario';
import { createDeliveryDispatchClientModule } from './deliverydispatch-client-module';
import type { ZLinkChannelClient } from '@zlink-systems/framework';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const DeliveryDispatchClientModule = createDeliveryDispatchClientModule(config);
  const app = await NestFactory.createApplicationContext(DeliveryDispatchClientModule, {
    logger: false,
    abortOnError: false
  });
  try {
    const channels = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
    await new DeliveryDispatchClientScenario().run(channels);
  } finally {
    await app.close();
  }
  console.log('PASS DeliveryDispatch.Ts');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
