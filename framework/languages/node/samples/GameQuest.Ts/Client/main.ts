import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { loadSampleConfig } from './Configuration/sample-config';
import { GameQuestClientScenario } from './gamequest-client-scenario';
import { createGameQuestClientModule } from './gamequest-client-module';
import type { ZLinkChannelClient } from '@zlink-systems/framework';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const GameQuestClientModule = createGameQuestClientModule(config);
  const app = await NestFactory.createApplicationContext(GameQuestClientModule, {
    logger: false,
    abortOnError: false
  });
  try {
    const channels = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
    await new GameQuestClientScenario().run(channels);
  } finally {
    await app.close();
  }
  console.log('PASS GameQuest.Ts');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
