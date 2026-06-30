import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkRegistryQuery } from '@zlink-systems/framework';
import { ZLINK_REGISTRY_QUERY, ZLinkRegistryModule } from '@zlink-systems/nestjs';
import { parseServerOptions } from './Configuration/server-options';
import { createRegistryMessagingEndpoints } from './Endpoints/registry-messaging-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startRegistryHost(args: readonly string[]): Promise<void> {
  const options = parseServerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;

  class RegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: options.registryPubEndpoint,
        routerEndpoint: options.registryRouterEndpoint
      })
    ],
    providers: [{ provide: EvidenceStore, useValue: evidence }]
  })(RegistryModule);

  const app = await NestFactory.createApplicationContext(RegistryModule, { logger: false, abortOnError: false });
  const query = app.get(ZLINK_REGISTRY_QUERY, { strict: false }) as ZLinkRegistryQuery;
  const server = await startHttpServer(
    options.httpUrl,
    createRegistryMessagingEndpoints(options, query, evidence, () => { stopping = true; })
  );
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
