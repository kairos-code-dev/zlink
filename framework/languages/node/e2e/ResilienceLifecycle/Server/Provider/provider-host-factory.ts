import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import {
  ZLINK_CHANNEL_CLIENT,
  ZLINK_CHANNEL_RUNTIME_OPTIONS,
  ZLinkModule,
  zlinkFramework
} from '@zlink-systems/nestjs';
import type { ZLinkChannelClient, ZLinkChannelRuntimeOptions } from '@zlink-systems/framework';
import { PacketNames } from '../../Shared/messages';
import { parseServerOptions } from './Configuration/server-options';
import type { ServerOptions } from './Configuration/server-options';
import { createProviderEndpoints } from './Endpoints/provider-endpoints';
import {
  EvidenceDispatchErrorObserver,
  PayloadRequestHandler,
  ProfileCommandHandler,
  ProfileRequestHandler,
} from './Handlers/provider-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { FaultState } from './Infrastructure/fault-state';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startProviderHost(args: readonly string[]): Promise<void> {
  const options = parseServerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  const fault = new FaultState();
  let stopping = false;

  const ProviderModule = createProviderModule(options, evidence, fault);
  const app = await NestFactory.createApplicationContext(ProviderModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const runtimeOptions = app.get(ZLINK_CHANNEL_RUNTIME_OPTIONS, { strict: false }) as ZLinkChannelRuntimeOptions;
  const server = await startHttpServer(options.httpUrl, createProviderEndpoints(
    evidence,
    fault,
    channel,
    runtimeOptions,
    () => { stopping = true; }
  ));

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createProviderModule(options: ServerOptions, evidence: EvidenceStore, fault: FaultState): Function {
  class ProviderModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .setMessageFlowObserver(EvidenceDispatchErrorObserver)
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);

          if (options.registryRouterEndpoint !== undefined) {
            builder.useDiscovery().addRegistryEndpoint(options.registryRouterEndpoint);
          }
          if (options.channelEndpoint !== undefined) {
            builder.addClientServerChannel('profile')
              .enableServer(options.channelEndpoint)
              .routingId(options.rid)
              .enableClient()
              .addRequestHandler(PacketNames.profileRequest, ProfileRequestHandler)
              .addRequestHandler(PacketNames.payloadRequest, PayloadRequestHandler)
              .addSendHandler(PacketNames.profileCommand, ProfileCommandHandler);
          }
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      { provide: FaultState, useValue: fault },
      EvidenceDispatchErrorObserver,
      PayloadRequestHandler,
      ProfileCommandHandler,
      ProfileRequestHandler,
    ]
  })(ProviderModule);
  return ProviderModule;
}
