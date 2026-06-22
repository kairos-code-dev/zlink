import 'reflect-metadata';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';
import { loadSampleConfig } from './Configuration/sample-config';
import { createGameQuestModule } from './QuestMission/gamequest-quest-module';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const RegistryModule = createRegistryModule(config);
  const registry = await NestFactory.createApplicationContext(RegistryModule, {
    logger: false,
    abortOnError: false
  });
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
  await registry.close();
}

function createRegistryModule(config: { registryPubEndpoint: string; registryRouterEndpoint: string }) {
  class GameQuestRegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: config.registryPubEndpoint,
        routerEndpoint: config.registryRouterEndpoint
      })
    ]
  })(GameQuestRegistryModule);
  return GameQuestRegistryModule;
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
