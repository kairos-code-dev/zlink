import 'reflect-metadata';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';
import { loadSampleConfig } from './Configuration/sample-config';
import { createShoppingMallModule } from './OrderWorkflow/shoppingmall-workflow-module';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const RegistryModule = createRegistryModule(config);
  const registry = await NestFactory.createApplicationContext(RegistryModule, {
    logger: false,
    abortOnError: false
  });
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
  await registry.close();
}

function createRegistryModule(config: { registryPubEndpoint: string; registryRouterEndpoint: string }) {
  class ShoppingMallRegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: config.registryPubEndpoint,
        routerEndpoint: config.registryRouterEndpoint
      })
    ]
  })(ShoppingMallRegistryModule);
  return ShoppingMallRegistryModule;
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
