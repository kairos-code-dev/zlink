import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkModule, ZLinkRegistryModule, zlinkFramework } from '@zlink-systems/nestjs';
import { parseRegistryOptions } from './Configuration/registry-options';
import { createRegistryEndpoints } from './Endpoints/registry-endpoints';
import { RegistryEventRecorder } from './Handlers/registry-event-recorders';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startRegistryHost(args: readonly string[]): Promise<void> {
  const options = parseRegistryOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;

  class RegistryModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        pubEndpoint: options.registryPubEndpoint,
        routerEndpoint: options.registryRouterEndpoint
      }),
      ZLinkModule.forRootFactory({
        useFactory: () => ({
          ...zlinkFramework().build(),
          monitoring: {
            registry: [{ sourceName: 'registry', intervalMs: 250 }]
          }
        })
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      RegistryEventRecorder
    ]
  })(RegistryModule);

  const app = await NestFactory.createApplicationContext(RegistryModule, { logger: false, abortOnError: false });
  const server = await startHttpServer(options.httpUrl, createRegistryEndpoints(evidence, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}
