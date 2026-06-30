import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode, ZLinkSocketEventKind } from '@zlink-systems/framework';
import type { ZLinkChannelRuntimeOptions } from '@zlink-systems/framework';
import {
  ZLINK_CHANNEL_RUNTIME_OPTIONS,
  ZLinkModule,
  zlinkFramework
} from '@zlink-systems/nestjs';
import { PacketNames, RuntimeMonitoringNames } from '../../Shared/messages';
import { parseServiceOptions } from './Configuration/service-options';
import type { ServiceOptions } from './Configuration/service-options';
import { createServiceEndpoints } from './Endpoints/service-endpoints';
import {
  FailingTimerHandler,
  MonitoringEntrySpot,
  ProfileRequestHandler,
  SocketEventRecorder,
  SpotEventRecorder,
  ThrowingSocketEventRecorder
} from './Handlers/service-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startServiceHost(args: readonly string[]): Promise<void> {
  const options = parseServiceOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;

  const ServiceModule = createServiceModule(options, evidence);
  const app = await NestFactory.createApplicationContext(ServiceModule, { logger: false, abortOnError: false });
  const runtimeOptions = app.get(ZLINK_CHANNEL_RUNTIME_OPTIONS, { strict: false }) as ZLinkChannelRuntimeOptions;
  const server = await startHttpServer(
    options.httpUrl,
    createServiceEndpoints(evidence, runtimeOptions, () => { stopping = true; })
  );

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createServiceModule(options: ServiceOptions, evidence: EvidenceStore): Function {
  class ServiceModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .codecs()
              .addJson()
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);

          builder.useDiscovery().addRegistryEndpoint(options.registryRouterEndpoint);
          builder.addClientServerChannel(RuntimeMonitoringNames.channel)
            .enableServer(options.channelEndpoint)
            .routingId(options.rid)
            .addRequestHandler(PacketNames.profileRequest, ProfileRequestHandler);
          builder.addSpotMesh(RuntimeMonitoringNames.spotChannel)
            .routingId(options.rid)
            .enableRouter(options.spotRouterEndpoint)
            .enablePubSub(options.spotPubEndpoint)
            .addEntrySpot(MonitoringEntrySpot);

          return {
            ...builder.build(),
            monitoring: {
              socket: [{
                sourceName: RuntimeMonitoringNames.channelServerSource,
                ...(options.socketFilter ? { events: [ZLinkSocketEventKind.ConnectionReady] } : {})
              }],
              spot: [{ sourceName: RuntimeMonitoringNames.spotNode, intervalMs: 100 }]
            }
          };
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      FailingTimerHandler,
      MonitoringEntrySpot,
      ProfileRequestHandler,
      SocketEventRecorder,
      SpotEventRecorder,
      ...(options.throwMonitor ? [ThrowingSocketEventRecorder] : [])
    ]
  })(ServiceModule);
  return ServiceModule;
}
