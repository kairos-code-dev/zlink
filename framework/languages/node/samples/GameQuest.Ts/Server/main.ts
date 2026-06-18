import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { loadSampleConfig } from './Configuration/sample-config';
import { createGameQuestModule } from './QuestMission/gamequest-quest-module';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const GameQuestModule = createGameQuestModule(config);
  const app = await NestFactory.createApplicationContext(GameQuestModule, {
    logger: false,
    abortOnError: false
  });
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.questEndpoint
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
