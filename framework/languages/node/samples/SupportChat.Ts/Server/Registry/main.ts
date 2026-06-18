import 'reflect-metadata';
import { waitForShutdown } from '../runtime-support';
import { loadSampleConfig } from '../Configuration/sample-config';
import { createSupportChatRegistryServer } from './registry-server-host';
async function bootstrap(): Promise<void> {
  const config = loadSampleConfig();
  const registryServer = createSupportChatRegistryServer(config);
  await registryServer.start();

  process.stdout.write(`${JSON.stringify({
    event: 'ready',
    pubEndpoint: config.registryPubEndpoint,
    routerEndpoint: config.registryRouterEndpoint
  })}\n`);

  try {
    await waitForShutdown({ keepAlive: true });
  } finally {
    await registryServer.close();
  }
}

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
