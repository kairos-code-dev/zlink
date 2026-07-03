import fs from 'node:fs';
import { createRedisLocationStore } from '../../Shared/location-store';
import { parseLocationProbeOptions } from './Configuration/location-probe-options';
import { createLocationProbeEndpoints } from './Endpoints/location-probe-endpoints';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startLocationProbeHost(args: readonly string[]): Promise<void> {
  const options = parseLocationProbeOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const store = createRedisLocationStore(options);
  let stopping = false;

  const server = await startHttpServer(options.httpUrl, createLocationProbeEndpoints(options, store, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  try {
    await store.dispose();
  } catch {
  }
}
