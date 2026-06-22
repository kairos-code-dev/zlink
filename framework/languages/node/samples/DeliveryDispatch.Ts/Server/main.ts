import 'reflect-metadata';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';
import { loadSampleConfig } from './Configuration/sample-config';
import { createDeliveryDispatchModule } from './DispatchCenter/deliverydispatch-dispatch-module';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const RegistryModule = createRegistryModule(config);
  const registry = await NestFactory.createApplicationContext(RegistryModule, {
    logger: false,
    abortOnError: false
  });
  const DeliveryDispatchModule = createDeliveryDispatchModule(config);
  const app = await NestFactory.createApplicationContext(DeliveryDispatchModule, {
    logger: false,
    abortOnError: false
  });
  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    endpoint: config.dispatchEndpoint
  })}\n`);
  await waitForShutdown();
  await closeNestRuntime(app);
  await closeNestRuntime(registry);
}

function createRegistryModule(config: { registryPubEndpoint: string; registryRouterEndpoint: string }) {
  class DeliveryDispatchRegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: config.registryPubEndpoint,
        routerEndpoint: config.registryRouterEndpoint
      })
    ]
  })(DeliveryDispatchRegistryModule);
  return DeliveryDispatchRegistryModule;
}

async function closeNestRuntime(container: { close(): Promise<void> }): Promise<void> {
  try {
    await container.close();
  } catch (error) {
    const candidate = error as { name?: string; code?: number };
    if (candidate.name === 'CloseError' && (candidate.code === 0 || candidate.code === 401)) {
      return;
    }
    throw error;
  }
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
