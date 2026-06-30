import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkRegistryQuery } from '@zlink-systems/framework';
import { ZLINK_REGISTRY_QUERY, ZLinkRegistryModule } from '@zlink-systems/nestjs';
import { parseRegistryOptions } from './Configuration/registry-options';
import { createRegistryEndpoints } from './Endpoints/registry-endpoints';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startRegistryHost(args: readonly string[]): Promise<void> {
  const options = parseRegistryOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  let stopping = false;

  class RegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        registryId: options.registryId,
        pubEndpoint: options.registryPubEndpoint,
        routerEndpoint: options.registryRouterEndpoint,
        peers: options.peers
      })
    ]
  })(RegistryModule);

  const app = await NestFactory.createApplicationContext(RegistryModule, { logger: false, abortOnError: false });
  const query = app.get(ZLINK_REGISTRY_QUERY, { strict: false }) as ZLinkRegistryQuery;
  const server = await startHttpServer(options.httpUrl, createRegistryEndpoints(options, query, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
