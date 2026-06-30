import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ChannelNames, PacketNames } from '../../Shared/messages';
import { parseProviderOptions } from './Configuration/provider-options';
import type { ProviderOptions } from './Configuration/provider-options';
import { createProviderEndpoints } from './Endpoints/provider-endpoints';
import { ProfileRequestHandler } from './Handlers/profile-request-handler';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startProviderHost(args: readonly string[]): Promise<void> {
  const options = parseProviderOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.rid, options.evidenceFile);
  let stopping = false;

  const ProviderModule = createProviderModule(options, evidence);
  const app = await NestFactory.createApplicationContext(ProviderModule, { logger: false, abortOnError: false });
  const server = await startHttpServer(options.httpUrl, createProviderEndpoints(evidence, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createProviderModule(options: ProviderOptions, evidence: EvidenceStore): Function {
  class ProviderModule {}
  Module({
    imports: [
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
            .addRequestHandler(PacketNames.profileRequest, ProfileRequestHandler);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      ProfileRequestHandler
    ]
  })(ProviderModule);
  return ProviderModule;
}
