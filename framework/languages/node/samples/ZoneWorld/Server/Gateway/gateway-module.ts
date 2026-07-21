import { zlinkFramework, zlinkModule, ZLinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZONEWORLD_CONFIG, createZoneWorldConfigurationModule } from '../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../Configuration/configuration';
import { createZoneWorldLocationStore, zoneWorldLocationOptions } from '../Configuration/location-store';
import { ZoneWorldNames } from '../../Shared/spec';
import { JoinWorldSessionHandler, PlayerSessionFactory } from './player-session';
import { GatewaySpotEventHandler } from './gateway-runtime-events';

function createGatewayModule() {
  class GatewayModule {}
  const configuration = createZoneWorldConfigurationModule('gateway');
  zlinkModule(__dirname, {
    imports: [configuration, ZLinkModule.forRootFactory({
      imports: [configuration],
      inject: [ZONEWORLD_CONFIG],
      useFactory: (value: unknown) => {
        const config = value as ZoneWorldConfiguration;
        const gateway = config.gateway;
        if (gateway === undefined) throw new Error('Gateway configuration is required.');
        const builder = zlinkFramework();
        builder.addLocationStore(createZoneWorldLocationStore(config.shared));
        zoneWorldLocationOptions(builder.configureLocations());
        builder.configureDispatch()
          .messageFlow(ZLinkMessageFlowLogMode.ErrorsOnly)
          .traceLabel('gateway');
        const zoneMesh = builder
          .addRouteMesh(ZoneWorldNames.zoneMesh)
            .useAllocatedRoutingId(1, 'gw0')
            .setRoutingIdAllocationGroup('zoneworld.gateway')
            .listen(gateway.spotRouterEndpoint);
        zoneMesh.channelName(ZoneWorldNames.zoneMesh).setWeight(0);
        zoneMesh.channelName(ZoneWorldNames.bridgeMesh).setWeight(0);
        zoneMesh.channelName(ZoneWorldNames.actorsChannel).setWeight(0);
        zoneMesh.channelName(ZoneWorldNames.reportChannel).setWeight(0);
        for (const nodeId of ['zone-node-1', 'zone-node-2']) {
          zoneMesh.channelName(ZoneWorldNames.opsChannel(nodeId)).setWeight(0);
        }
        builder.addStreamNode(ZoneWorldNames.gatewayStreamNode)
          .bind(gateway.streamEndpoint)
          .registerSession(PlayerSessionFactory);
        const registration = builder.build();
        return {
          ...registration,
          monitoring: { spot: [{ sourceName: ZoneWorldNames.zoneMesh, intervalMs: 50 }] }
        };
      }
    })],
    providers: [GatewaySpotEventHandler, JoinWorldSessionHandler, PlayerSessionFactory]
  })(GatewayModule);
  return GatewayModule;
}

export { createGatewayModule };
