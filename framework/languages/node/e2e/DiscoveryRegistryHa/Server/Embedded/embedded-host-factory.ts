import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode, type ZLinkRegistryQuery } from '@zlink-systems/framework';
import { ZLINK_REGISTRY_QUERY, ZLinkModule, ZLinkRegistryModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ChannelNames, PacketNames } from '../../Shared/messages';
import { ProfileRequestHandler } from '../Provider/Handlers/profile-request-handler';
import { EvidenceStore } from '../Provider/Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from '../Registry/Support/http-server';
import { parseEmbeddedOptions } from './Configuration/embedded-options';
import type { EmbeddedOptions } from './Configuration/embedded-options';
import { createEmbeddedEndpoints } from './Endpoints/embedded-endpoints';

export async function startEmbeddedHost(args: readonly string[]): Promise<void> {
  const options = parseEmbeddedOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  let stopping = false;

  const EmbeddedModule = createEmbeddedModule(options, evidence);
  const app = await NestFactory.createApplicationContext(EmbeddedModule, { logger: false, abortOnError: false });
  const query = app.get(ZLINK_REGISTRY_QUERY, { strict: false }) as ZLinkRegistryQuery;
  const server = await startHttpServer(options.httpUrl, createEmbeddedEndpoints(options, query, evidence, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createEmbeddedModule(options: EmbeddedOptions, evidence: EvidenceStore): Function {
  class EmbeddedModule {}
  Module({
    imports: [
      ZLinkRegistryModule.forRoot({
        registryId: options.registryId,
        pubEndpoint: options.registryPubEndpoint,
        routerEndpoint: options.registryRouterEndpoint,
        peers: options.peers
      }),
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          for (const endpoint of options.registryRouterEndpoints) {
            builder.useDiscovery().addRegistryEndpoint(endpoint);
          }
          builder.addClientServerChannel(ChannelNames.profile)
            .enableServer(options.channelEndpoint)
            .routingId(options.rid)
            .addRequestHandler(PacketNames.profileReq, ProfileRequestHandler);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      ProfileRequestHandler
    ]
  })(EmbeddedModule);
  return EmbeddedModule;
}
