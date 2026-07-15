import { zlinkFramework, zlinkModule, ZLinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import {
  ZONEWORLD_CONFIG,
  createZoneWorldConfigurationModule
} from '../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../Configuration/configuration';
import { createZoneWorldLocationStore, zoneWorldLocationOptions } from '../Configuration/location-store';
import { ZoneWorldNames, zonesOf } from '../../Shared/spec';
import { PlayerActor } from './Infrastructure/ZLink/Actors/player-actor';
import { PlayerActorFactory } from './Infrastructure/ZLink/Actors/player-actor-factory';
import { PlayerActorTransferAdapter } from './Infrastructure/ZLink/Actors/player-actor-transfer-adapter';
import { ZoneEntrySpot } from './Infrastructure/ZLink/Spots/zone-entry-spot';
import { ZoneSpot } from './Infrastructure/ZLink/Spots/zone-spot';
import {
  ApplyNodeMaintenanceHandler,
  EnsurePlayerActorHandler,
  GetNodeDiagnosticsHandler,
  MaintenanceChangedSubscriber,
  WorldAnnounceSubscriber
} from './Infrastructure/ZLink/Handlers/node-channel-handlers';
import { PacketNames } from '../../Shared/contracts';
import {
  EntryJoinWorldHandler,
  PlayerMoveHandler,
  ZoneJoinWorldHandler
} from './Infrastructure/ZLink/Handlers/player-handlers';
import {
  FirstBorderSubscriptionHandler,
  DeliverAnnounceHandler,
  SecondBorderSubscriptionHandler,
  ZoneTickHandler
} from './Infrastructure/ZLink/Handlers/zone-runtime-handlers';

const ZONE_NODE_ALLOCATION_GROUP = 'zoneworld.zone-node';

function createZoneNodeModule() {
  class ZoneNodeModule {}
  const configuration = createZoneWorldConfigurationModule('zoneNode');
  zlinkModule(__dirname, {
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [ZONEWORLD_CONFIG],
        useFactory: (value: unknown) => {
          const config = value as ZoneWorldConfiguration;
          const node = config.zoneNode;
          if (node === undefined) throw new Error('ZoneNode configuration is required.');
          const builder = zlinkFramework();
          builder.addLocationStore(createZoneWorldLocationStore(config.shared));
          Object.assign(builder.configureLocations(), zoneWorldLocationOptions());
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.ErrorsOnly)
            .traceLabel(node.nodeId);

          if (zonesOf(node.nodeId).length === 0) {
            builder.addFanoutChannel(ZoneWorldNames.broadcastChannel)
              .enableSubscriber()
              .addPublishHandler(PacketNames.worldAnnounceEvent, WorldAnnounceSubscriber)
              .addPublishHandler(PacketNames.nodeMaintenanceChangedEvent, MaintenanceChangedSubscriber);
            return builder.build();
          }

          builder.addActorTransferAdapter(PlayerActor, PlayerActorTransferAdapter);
          builder.addSpotMesh(ZoneWorldNames.zoneMesh)
            .useAllocatedRoutingId(2, 'zn')
            .setRoutingIdAllocationGroup(ZONE_NODE_ALLOCATION_GROUP)
            .enableRouter(node.spotRouterEndpoint)
            .enablePubSub(node.spotPubSubEndpoint)
            .addEntrySpot(ZoneEntrySpot)
            .actorFactory(ZoneWorldNames.playerActorType, PlayerActorFactory)
            .addSpotFactory(ZoneSpot);
          builder.addRouteMeshChannel(ZoneWorldNames.bridgeMesh)
            .useAllocatedRoutingId(2, 'zn')
            .setRoutingIdAllocationGroup(ZONE_NODE_ALLOCATION_GROUP)
            .enableRouter(node.bridgeEndpoint)
            .enableClient();
          builder.addClientServerChannel(ZoneWorldNames.reportChannel)
            .useAllocatedRoutingId(2, 'zn')
            .setRoutingIdAllocationGroup(ZONE_NODE_ALLOCATION_GROUP)
            .enableClient();
          builder.addClientServerChannel(ZoneWorldNames.opsChannel(node.nodeId))
            .enableServer(node.opsChannelEndpoint)
            .addRequestHandler(PacketNames.applyNodeMaintenanceReq, ApplyNodeMaintenanceHandler)
            .addRequestHandler(PacketNames.getNodeDiagnosticsReq, GetNodeDiagnosticsHandler);
          if (node.nodeId === 'zone-node-1') {
            builder.addClientServerChannel(ZoneWorldNames.actorsChannel)
              .enableServer(node.actorsChannelEndpoint)
              .addRequestHandler(PacketNames.ensurePlayerActorReq, EnsurePlayerActorHandler);
          }
          builder.addFanoutChannel(ZoneWorldNames.broadcastChannel)
            .enableSubscriber()
            .addPublishHandler(PacketNames.worldAnnounceEvent, WorldAnnounceSubscriber)
            .addPublishHandler(PacketNames.nodeMaintenanceChangedEvent, MaintenanceChangedSubscriber);
          return builder.build();
        }
      })
    ],
    providers: [
      PlayerActorFactory,
      PlayerActorTransferAdapter,
      ZoneEntrySpot,
      ZoneSpot,
      ApplyNodeMaintenanceHandler,
      EnsurePlayerActorHandler,
      GetNodeDiagnosticsHandler,
      MaintenanceChangedSubscriber,
      WorldAnnounceSubscriber,
      EntryJoinWorldHandler,
      PlayerMoveHandler,
      ZoneJoinWorldHandler,
      FirstBorderSubscriptionHandler,
      DeliverAnnounceHandler,
      SecondBorderSubscriptionHandler,
      ZoneTickHandler
    ]
  })(ZoneNodeModule);
  return ZoneNodeModule;
}

export { ZONE_NODE_ALLOCATION_GROUP, createZoneNodeModule };
