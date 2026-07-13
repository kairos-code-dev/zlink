import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { EvidenceStore } from './Configuration/evidence-store';
import { loadSampleConfig } from './Configuration/sample-config';
import { createTrackingModule } from './Tracking/tracking-module';
import { createSessionModule } from './Session/session-module';
import { createCourierActorNodeModule } from './Courier/courier-module';
import { createCourierSessionModule } from './CourierSession/courier-session-module';
import { createDispatchCenterModule } from './DispatchCenter/dispatch-center-module';
import { createDispatchApiModule } from './DispatchApi/dispatch-api-module';
import { startDispatchApi } from './DispatchApi/dispatch-api-server';
import { waitForTopology } from './Probe/probe';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  const role = readOption('--role') ?? process.env.DELIVERYDISPATCH_ROLE ?? 'all';
  const evidence = new EvidenceStore();

  if (role === 'probe') {
    await waitForTopology(config, Number(readOption('--timeout-ms') ?? 10000));
    process.exit(0);
    return;
  }

  const modules = [];
  if (role === 'all' || role === 'tracking') {
    modules.push(createTrackingModule(config, evidence));
  }
  if (role === 'all' || role === 'customer-gateway') {
    modules.push(createSessionModule(config));
  }
  if (role === 'all' || role === 'courier-session') {
    modules.push(createCourierSessionModule(config));
  }
  if (role === 'all' || role === 'courier-spot-node1') {
    modules.push(createCourierActorNodeModule(config, {
      courierId: 'courier-a'
    }));
  }
  if (role === 'all' || role === 'courier-spot-node2') {
    modules.push(createCourierActorNodeModule(config, {
      courierId: 'courier-b'
    }));
  }
  if (role === 'all' || role === 'dispatch') {
    modules.push(createDispatchCenterModule(config));
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

  const httpServer = (role === 'all' || role === 'dispatch')
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
