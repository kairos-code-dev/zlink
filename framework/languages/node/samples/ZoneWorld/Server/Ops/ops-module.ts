import { zlinkFramework, zlinkModule, ZLinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZONEWORLD_CONFIG, createZoneWorldConfigurationModule } from '../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../Configuration/configuration';
import { createZoneWorldLocationStore, zoneWorldLocationOptions } from '../Configuration/location-store';
import { NodeIds, ZoneWorldNames } from '../../Shared/spec';
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
  OpsLocationEventHandler,
  OpsReportMeshEventHandler
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
        zoneWorldLocationOptions(builder.configureLocations());
        builder.configureDispatch().messageFlow(ZLinkMessageFlowLogMode.ErrorsOnly).traceLabel('ops');
        builder.addStreamNode(ZoneWorldNames.opsStreamNode)
          .bind(ops.streamEndpoint)
          .registerSession(OpsSessionFactory);
        builder.addFanoutChannel(ZoneWorldNames.broadcastChannel).enablePublisher(ops.broadcastEndpoint);
        const mesh = builder.addRouteMesh(ZoneWorldNames.zoneMesh)
          .listen(ops.reportEndpoint);
        mesh.channelName(ZoneWorldNames.reportChannel).addHandlerGroup('ops');
        mesh.channelName(ZoneWorldNames.zoneMesh).setWeight(0);
        mesh.channelName(ZoneWorldNames.bridgeMesh).setWeight(0);
        mesh.channelName(ZoneWorldNames.actorsChannel).setWeight(0);
        for (const nodeId of [NodeIds.west, NodeIds.east]) {
          const channelName = ZoneWorldNames.opsChannel(nodeId);
          mesh.channelName(channelName).setWeight(0);
        }
        return {
          ...builder.build(),
          monitoring: {
            locationRuntime: [{ sourceName: OPS_LOCATION_SOURCE, intervalMs: 100 }],
            spot: [{ sourceName: ZoneWorldNames.zoneMesh, intervalMs: 100 }]
          }
        };
      }
    })],
    providers: [
      NodeRegistry,
      MaintenanceStore,
      OpsLocationEventHandler,
      OpsReportMeshEventHandler,
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
