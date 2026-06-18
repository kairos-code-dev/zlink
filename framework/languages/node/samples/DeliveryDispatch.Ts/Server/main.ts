import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { loadSampleConfig } from './Configuration/sample-config';
import { createDeliveryDispatchModule } from './DispatchCenter/deliverydispatch-dispatch-module';

async function main(): Promise<void> {
  const config = loadSampleConfig();
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
