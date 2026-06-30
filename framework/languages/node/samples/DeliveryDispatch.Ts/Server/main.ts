import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { EvidenceStore } from './Configuration/evidence-store';
import { loadSampleConfig } from './Configuration/sample-config';
import { createRegistryModule } from './Registry/registry-module';
import { createTrackingModule } from './Tracking/tracking-module';
import { createSessionModule } from './Session/session-module';
import { createCourierModule } from './Courier/courier-module';
import { createDispatchCenterModule } from './DispatchCenter/dispatch-center-module';
import { createDispatchApiModule } from './DispatchApi/dispatch-api-module';
import { startDispatchApi } from './DispatchApi/dispatch-api-server';
import { waitForTopology } from './Probe/probe';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const role = readOption('--role') ?? process.env.DELIVERYDISPATCH_ROLE ?? 'all';
  const evidence = new EvidenceStore();

  if (role === 'probe') {
    await waitForTopology(config.registryRouterEndpoint, Number(readOption('--timeout-ms') ?? 10000));
    return;
  }

  const modules = [];
  if (role === 'all' || role === 'registry') {
    modules.push(createRegistryModule(config));
  }
  if (role === 'all' || role === 'tracking') {
    modules.push(createTrackingModule(config, evidence));
  }
  if (role === 'all' || role === 'session') {
    modules.push(createSessionModule(config));
  }
  if (role === 'all' || role === 'courier-a') {
    modules.push(createCourierModule(config, {
      courierId: 'courier-a',
      mode: readOption('--mode') ?? process.env.DELIVERYDISPATCH_COURIER_MODE ?? 'timeout-reassign'
    }));
  }
  if (role === 'all' || role === 'courier-b') {
    modules.push(createCourierModule(config, {
      courierId: 'courier-b',
      mode: readOption('--mode') ?? process.env.DELIVERYDISPATCH_COURIER_MODE ?? 'accept'
    }));
  }
  if (role === 'all' || role === 'dispatch-center') {
    modules.push(createDispatchCenterModule(config));
  }
  if (role === 'all' || role === 'dispatch-api') {
    modules.push(createDispatchApiModule(config));
  }

  if (modules.length === 0) {
    throw new Error(`Unknown DeliveryDispatch role '${role}'.`);
  }

  const apps = [];
  for (const moduleType of modules) {
    apps.push(await NestFactory.createApplicationContext(moduleType, {
      logger: false,
      abortOnError: false
    }));
  }

  const httpServer = (role === 'all' || role === 'dispatch-api')
    ? await startDispatchApi(apps[apps.length - 1], config, evidence)
    : undefined;

  process.stdout.write(`${JSON.stringify({ event: 'ready', role })}\n`);
  await waitForShutdown();

  if (httpServer !== undefined) {
    await new Promise<void>((resolve) => httpServer.close(() => resolve()));
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
