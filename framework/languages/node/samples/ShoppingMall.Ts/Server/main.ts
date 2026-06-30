import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { loadSampleConfig } from './Configuration/sample-config';
import { createCommerceApiModule } from './CommerceApi/commerce-api-module';
import { startCommerceApi } from './CommerceApi/commerce-api-server';
import { createShoppingMallModule } from './OrderWorkflow/shoppingmall-workflow-module';
import { createRegistryModule } from './Registry/registry-module';
import type http from 'node:http';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const role = readOption('--role') ?? process.env.SHOPPINGMALL_ROLE ?? 'all';
  const modules = [];
  const httpServers: http.Server[] = [];

  if (role === 'all' || role === 'registry') {
    modules.push(createRegistryModule(config));
  }
  if (role === 'all' || role === 'workflow-a') {
    modules.push(createShoppingMallModule(config, config.workflowAEndpoint, 'workflow-a'));
  }
  if (role === 'all' || role === 'workflow-b') {
    modules.push(createShoppingMallModule(config, config.workflowBEndpoint, 'workflow-b'));
  }
  if (role === 'all' || role === 'api-a') {
    modules.push(createCommerceApiModule(config, 'api-a'));
  }
  if (role === 'all' || role === 'api-b') {
    modules.push(createCommerceApiModule(config, 'api-b'));
  }
  if (modules.length === 0) {
    throw new Error(`Unknown ShoppingMall role '${role}'.`);
  }

  const apps = [];
  for (const moduleType of modules) {
    apps.push(await NestFactory.createApplicationContext(moduleType, {
      logger: false,
      abortOnError: false
    }));
  }

  if (role === 'all') {
    httpServers.push(await startCommerceApi(apps[apps.length - 2], config, 'api-a'));
    httpServers.push(await startCommerceApi(apps[apps.length - 1], config, 'api-b'));
  } else if (role === 'api-a') {
    httpServers.push(await startCommerceApi(apps[0], config, 'api-a'));
  } else if (role === 'api-b') {
    httpServers.push(await startCommerceApi(apps[0], config, 'api-b'));
  }

  process.stdout.write(`${JSON.stringify({ event: 'ready', role })}\n`);
  await waitForShutdown();
  for (const server of httpServers.reverse()) {
    await new Promise<void>((resolve) => server.close(() => resolve()));
  }
  for (const app of apps.reverse()) {
    await closeNestRuntime(app);
  }
}

function readOption(name: string): string | undefined {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : undefined;
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
    const keepAlive = setInterval(() => undefined, 1000);
    const stop = () => {
      clearInterval(keepAlive);
      resolve();
    };
    process.once('SIGINT', stop);
    process.once('SIGTERM', stop);
  });
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
