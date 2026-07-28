import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import {
  ZLinkMessageFlowLogMode,
  ZLinkSocketEventKind,
  type ZLinkChannelRuntimeOptions,
  type ZLinkFrameworkRuntime,
  type ZLinkLocationRuntimeQuery
} from '@zlink-systems/framework';
import {
  ZLINK_CHANNEL_RUNTIME_OPTIONS,
  ZLINK_FRAMEWORK_RUNTIME,
  ZLINK_LOCATION_RUNTIME_QUERY,
  ZLinkModule,
  zlinkFramework
} from '@zlink-systems/nestjs';
import { PacketNames, RuntimeMonitoringNames } from '../../Shared/messages';
import { validateServiceOptions } from './Configuration/service-options';
import type { ServiceOptions, ServiceRoleOptions } from './Configuration/service-options';
import { MONITORING_OPTIONS, createMonitoringConfigurationModule } from '../../configuration';
import { createServiceEndpoints } from './Endpoints/service-endpoints';
import {
  FailingTimerHandler,
  MonitoringEntrySpot,
  ProfileRequestHandler,
  SocketEventRecorder,
  SpotEventRecorder,
  LocationRuntimeEventRecorder,
  ThrowingSocketEventRecorder
} from './Handlers/service-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { createRedisLocationStore, monitoringLocationOptions } from '../../Shared/location-store';

export async function startServiceHost(role: ServiceRoleOptions = {}): Promise<void> {
  let stopping = false;

  const ServiceModule = createServiceModule(role);
  const app = await NestFactory.createApplicationContext(ServiceModule, { logger: false, abortOnError: false });
  const options = app.get(MONITORING_OPTIONS, { strict: false }) as ServiceOptions;
  const evidence = app.get(EvidenceStore, { strict: false });
  const runtimeOptions = app.get(ZLINK_CHANNEL_RUNTIME_OPTIONS, { strict: false }) as ZLinkChannelRuntimeOptions;
  const frameworkRuntime = app.get(ZLINK_FRAMEWORK_RUNTIME, { strict: false }) as ZLinkFrameworkRuntime;
  const locations = app.get(ZLINK_LOCATION_RUNTIME_QUERY, { strict: false }) as ZLinkLocationRuntimeQuery;
  const server = await startHttpServer(
    options.httpUrl,
    createServiceEndpoints(evidence, runtimeOptions, frameworkRuntime, locations, () => { stopping = true; })
  );

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createServiceModule(role: ServiceRoleOptions): Function {
  class ServiceModule {}
  const configuration = createMonitoringConfigurationModule((value) => validateServiceOptions(value, role));

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration], inject: [MONITORING_OPTIONS],
        useFactory: (value: unknown) => {
          const options = value as ServiceOptions;
          fs.mkdirSync(options.logDir, { recursive: true });
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);

          builder.addLocationStore(createRedisLocationStore({
            redisEndpoint: options.redisEndpoint,
            redisKeyPrefix: options.redisKeyPrefix
          }));
          Object.assign(builder.configureLocations(), monitoringLocationOptions());
          const serviceMesh = builder.addRouteMesh(RuntimeMonitoringNames.channel)
            .listen(options.channelEndpoint)
            .routingId(options.rid);
          serviceMesh.channelName(RuntimeMonitoringNames.channel)
            .addRequestHandler(PacketNames.profileReq, ProfileRequestHandler);
          const spotMesh = builder.addRouteMesh(RuntimeMonitoringNames.spotChannel)
            .routingId(options.rid)
            .listen(options.spotRouterEndpoint);
          spotMesh.objects().server().addEntrySpot(MonitoringEntrySpot);
          spotMesh.channelName(RuntimeMonitoringNames.spotChannel);

          return {
            ...builder.build(),
            monitoring: {
              socket: [{
                sourceName: RuntimeMonitoringNames.channelServerSource,
                ...(options.socketFilter ? { events: [ZLinkSocketEventKind.ConnectionReady] } : {})
              }],
              locationRuntime: [{ sourceName: RuntimeMonitoringNames.locationRuntimeSource, intervalMs: 100 }]
            }
          };
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, inject: [MONITORING_OPTIONS], useFactory: (value: unknown) => {
        const options = value as ServiceOptions; return new EvidenceStore(options.rid, options.evidenceFile);
      } },
      FailingTimerHandler,
      MonitoringEntrySpot,
      ProfileRequestHandler,
      SocketEventRecorder,
      SpotEventRecorder,
      LocationRuntimeEventRecorder,
      ...(role.throwMonitor === true ? [ThrowingSocketEventRecorder] : [])
    ]
  })(ServiceModule);
  return ServiceModule;
}
