import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkRegistryModule } from '@zlink-systems/nestjs';
import { parseRegistryOptions } from './Configuration/server-options';
import { createOperationalEndpoints } from './Endpoints/operational-endpoints';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startRegistryHost(args: readonly string[]): Promise<void> {
  const options = parseRegistryOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid);
  let stopping = false;

  class RegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: options.registryPubEndpoint,
        routerEndpoint: options.registryRouterEndpoint
      })
    ]
  })(RegistryModule);

  const app = await NestFactory.createApplicationContext(RegistryModule, { logger: false, abortOnError: false });
  const server = await startHttpServer(options.httpUrl, createOperationalEndpoints(evidence, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
