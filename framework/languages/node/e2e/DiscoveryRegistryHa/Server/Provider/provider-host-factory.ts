import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ChannelNames, PacketNames } from '../../Shared/messages';
import { createRedisLocationStore, storeFailureLocationOptions } from '../../Shared/location-store';
import { validateProviderOptions } from './Configuration/provider-options';
import type { ProviderOptions } from './Configuration/provider-options';
import { DISCOVERY_OPTIONS, createDiscoveryConfigurationModule } from '../../configuration';
import { createProviderEndpoints } from './Endpoints/provider-endpoints';
import { ProfileRequestHandler } from './Handlers/profile-request-handler';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startProviderHost(): Promise<void> {
  let stopping = false;

  const ProviderModule = createProviderModule();
  const app = await NestFactory.createApplicationContext(ProviderModule, { logger: false, abortOnError: false });
  const options = app.get(DISCOVERY_OPTIONS, { strict: false }) as ProviderOptions;
  const evidence = app.get(EvidenceStore, { strict: false });
  const server = await startHttpServer(options.httpUrl, createProviderEndpoints(evidence, () => { stopping = true; }));
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createProviderModule(): Function {
  class ProviderModule {}
  const configuration = createDiscoveryConfigurationModule(validateProviderOptions);
  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration], inject: [DISCOVERY_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as ProviderOptions;
          fs.mkdirSync(options.logDir, { recursive: true });
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.addLocationStore(createRedisLocationStore(options));
          Object.assign(builder.configureLocations(), storeFailureLocationOptions());
          builder.addClientServerChannel(ChannelNames.profile)
            .enableServer(options.channelEndpoint)
            .routingId(options.rid)
            .addRequestHandler(PacketNames.profileReq, ProfileRequestHandler);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, inject: [DISCOVERY_OPTIONS], useFactory: (value: unknown) => {
        const options = value as ProviderOptions; return new EvidenceStore(options.rid, options.evidenceFile);
      } },
      ProfileRequestHandler
    ]
  })(ProviderModule);
  return ProviderModule;
}
