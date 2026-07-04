import { loadSampleConfig } from '../Configuration/sample-config';
import { waitForShutdown, closeServer } from '../runtime-support';
import { startRegistryServer } from './registry-server-host';

async function main(): Promise<void> {
  const server = startRegistryServer(loadSampleConfig());
  process.stdout.write(`${JSON.stringify({ event: 'ready', role: 'registry' })}\n`);
  await waitForShutdown();
  await closeServer(server);
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
