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
        Object.assign(builder.configureLocations(), zoneWorldLocationOptions());
        builder.configureDispatch()
          .messageFlow(ZLinkMessageFlowLogMode.ErrorsOnly)
          .traceLabel('gateway');
        const registration = builder
          .addClientServerChannel(ZoneWorldNames.actorsChannel)
            .enableClient()
          .addSpotMesh(ZoneWorldNames.zoneMesh)
            .useAllocatedRoutingId(1, 'gw0')
            .setRoutingIdAllocationGroup('zoneworld.gateway')
            .enableRouter(gateway.spotRouterEndpoint)
            .enablePubSub(gateway.spotPubSubEndpoint)
          .addStreamNode(ZoneWorldNames.gatewayStreamNode)
            .bind(gateway.streamEndpoint)
            .registerSession(PlayerSessionFactory)
          .build();
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
