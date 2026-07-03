import fs from 'node:fs';
import { createRedisLocationStore } from '../../Shared/location-store';
import { parseServerOptions } from './Configuration/server-options';
import { createLocationProbeEndpoints } from './Endpoints/location-probe-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startLocationProbeHost(args: readonly string[]): Promise<void> {
  const options = parseServerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  const store = createRedisLocationStore(options);
  let stopping = false;

  const server = await startHttpServer(
    options.httpUrl,
    createLocationProbeEndpoints(options, store, evidence, () => { stopping = true; })
  );
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  try {
    await store.dispose();
  } catch {
  }
}
