import { zlinkFramework, zlinkModule, ZLinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZONEWORLD_CONFIG, createZoneWorldConfigurationModule } from '../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../Configuration/configuration';
import { createZoneWorldLocationStore, zoneWorldLocationOptions } from '../Configuration/location-store';
import { NodeIds, ZoneWorldNames } from '../../Shared/spec';
import { PacketNames } from '../../Shared/contracts';
import { NodeRegistry } from './node-registry';
import {
  AnnounceWorldHandler,
  NodeDiagnosticsHandler,
  ReportNodeStatusHandler,
  ReportSpotEventHandler,
  SetMaintenanceHandler,
  WatchNodesHandler
} from './ops-handlers';
import { OpsSessionFactory } from './ops-session';
import { OpsConsoleRegistry } from './ops-console-registry';
import { MaintenanceStore } from '../Configuration/maintenance-store';
import {
  OPS_LOCATION_SOURCE,
  OPS_REPORT_SOCKET_SOURCE,
  OpsLocationEventHandler,
  OpsSocketEventHandler
} from './ops-runtime-events';

function createOpsModule() {
  class OpsModule {}
  const configuration = createZoneWorldConfigurationModule('ops');
  zlinkModule(__dirname, {
    imports: [configuration, ZLinkModule.forRootFactory({
      imports: [configuration],
      inject: [ZONEWORLD_CONFIG],
      useFactory: (value: unknown) => {
        const config = value as ZoneWorldConfiguration;
        const ops = config.ops;
        if (ops === undefined) throw new Error('Ops configuration is required.');
        const builder = zlinkFramework();
        builder.addLocationStore(createZoneWorldLocationStore(config.shared));
        Object.assign(builder.configureLocations(), zoneWorldLocationOptions());
        builder.configureDispatch().messageFlow(ZLinkMessageFlowLogMode.ErrorsOnly).traceLabel('ops');
        builder.addStreamNode(ZoneWorldNames.opsStreamNode)
          .bind(ops.streamEndpoint)
          .registerSession(OpsSessionFactory);
        builder.addFanoutChannel(ZoneWorldNames.broadcastChannel).enablePublisher(ops.broadcastEndpoint);
        builder.addClientServerChannel(ZoneWorldNames.reportChannel)
          .enableServer(ops.reportEndpoint)
          .addSendHandler(PacketNames.reportNodeStatusMsg, ReportNodeStatusHandler)
          .addSendHandler(PacketNames.reportSpotEventMsg, ReportSpotEventHandler);
        for (const nodeId of [NodeIds.west, NodeIds.east]) {
          builder.addClientServerChannel(ZoneWorldNames.opsChannel(nodeId)).enableClient();
        }
        return {
          ...builder.build(),
          monitoring: {
            locationRuntime: [{ sourceName: OPS_LOCATION_SOURCE, intervalMs: 100 }],
            socket: [{ sourceName: OPS_REPORT_SOCKET_SOURCE }]
          }
        };
      }
    })],
    providers: [
      NodeRegistry,
      MaintenanceStore,
      OpsLocationEventHandler,
      OpsSocketEventHandler,
      OpsConsoleRegistry,
      OpsSessionFactory,
      AnnounceWorldHandler,
      NodeDiagnosticsHandler,
      ReportNodeStatusHandler,
      ReportSpotEventHandler,
      SetMaintenanceHandler,
      WatchNodesHandler
    ]
  })(OpsModule);
  return OpsModule;
}

export { createOpsModule };
