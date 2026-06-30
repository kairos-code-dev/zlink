import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkRegistryQueryClient } from '@zlink-systems/framework';
import { ZLINK_REGISTRY_QUERY_CLIENT, ZLinkRegistryQueryClientModule } from '@zlink-systems/nestjs';
import { closeHttpServer, startHttpServer } from '../Registry/Support/http-server';
import { parseProbeOptions } from './Configuration/probe-options';
import { createProbeEndpoints } from './Endpoints/probe-endpoints';

export async function startProbeHost(args: readonly string[]): Promise<void> {
  const options = parseProbeOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  let stopping = false;

  class ProbeModule {}
  Module({
    imports: [
      ZLinkRegistryQueryClientModule.forRoot({ endpoint: options.registryRouterEndpoint })
    ]
  })(ProbeModule);

  const app = await NestFactory.createApplicationContext(ProbeModule, { logger: false, abortOnError: false });
  const query = app.get(ZLINK_REGISTRY_QUERY_CLIENT, { strict: false }) as ZLinkRegistryQueryClient;
  const server = await startHttpServer(options.httpUrl, createProbeEndpoints(options, query, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
