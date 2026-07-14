import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { createRedisLocationStore } from '../../Shared/location-store';
import { validateServerOptions, type ServerOptions } from './Configuration/server-options';
import { REGISTRY_MESSAGING_OPTIONS, createRegistryMessagingConfigurationModule } from '../../configuration';
import { createLocationProbeEndpoints } from './Endpoints/location-probe-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startLocationProbeHost(): Promise<void> {
  class LocationProbeModule {}
  const configuration = createRegistryMessagingConfigurationModule(validateServerOptions);
  Module({ imports: [configuration] })(LocationProbeModule);
  const app = await NestFactory.createApplicationContext(LocationProbeModule, { logger: false, abortOnError: false });
  const options = app.get(REGISTRY_MESSAGING_OPTIONS, { strict: false }) as ServerOptions;
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
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
  await app.close();
}
