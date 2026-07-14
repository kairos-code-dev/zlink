import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { createRedisLocationStore } from '../../Shared/location-store';
import { validateLocationProbeOptions, type LocationProbeOptions } from './Configuration/location-probe-options';
import { DISCOVERY_OPTIONS, createDiscoveryConfigurationModule } from '../../configuration';
import { createLocationProbeEndpoints } from './Endpoints/location-probe-endpoints';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startLocationProbeHost(): Promise<void> {
  class LocationProbeModule {}
  const configuration = createDiscoveryConfigurationModule(validateLocationProbeOptions);
  Module({ imports: [configuration] })(LocationProbeModule);
  const app = await NestFactory.createApplicationContext(LocationProbeModule, { logger: false, abortOnError: false });
  const options = app.get(DISCOVERY_OPTIONS, { strict: false }) as LocationProbeOptions;
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
  await app.close();
}
