import { zlinkFramework, zlinkModule, ZLinkModule } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import {
  ZONEWORLD_CONFIG,
  createZoneWorldConfigurationModule
} from '../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../Configuration/configuration';
import { createZoneWorldLocationStore, zoneWorldLocationOptions } from '../Configuration/location-store';
import { ZoneWorldNames, zonesOf } from '../../Shared/spec';
import { PlayerActorFactory } from './Infrastructure/ZLink/Actors/player-actor-factory';
import { DeliverZoneNotificationHandler } from './Infrastructure/ZLink/Actors/player-actor';
import { PlayerActorRelocationAdapter } from './Infrastructure/ZLink/Actors/player-actor-relocation-adapter';
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
  EntryEnterWorldHandler,
  EntryJoinWorldHandler,
  PlayerBotTickHandler,
  PlayerMoveHandler,
  PlayerMovement
} from './Infrastructure/ZLink/Handlers/player-handlers';
import {
  BotTickHandler,
  FirstBorderSubscriptionHandler,
  DeliverAnnounceHandler,
  SecondBorderSubscriptionHandler,
  UpdateZonePositionHandler,
  ZoneTickHandler
} from './Infrastructure/ZLink/Handlers/zone-runtime-handlers';
import { MaintenanceStore } from '../Configuration/maintenance-store';
import { NodeRuntimeState } from './Domain/node-runtime-state';
import { SpotRuntimeEventHandler } from './Infrastructure/ZLink/Handlers/spot-runtime-event-handler';


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
          zoneWorldLocationOptions(builder.configureLocations());
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

          const zoneMesh = builder.addRouteMesh(ZoneWorldNames.zoneMesh)
            .setRoutingIdPrefix('zn')
            .listen(node.spotRouterEndpoint);
          const objectServer = zoneMesh.objects().server();
          objectServer.addEntrySpot(ZoneEntrySpot);
          objectServer.addSpotFactory(
            ZoneSpot.name,
            ZoneSpot,
            (factory) => factory.disableRelocation()
          );
          objectServer.addActorFactory(
            ZoneWorldNames.playerActorType,
            PlayerActorFactory,
            (factory) => factory.preserveStateWith(PlayerActorRelocationAdapter)
          );
          zoneMesh.channelName(ZoneWorldNames.zoneMesh);
          zoneMesh.channelName(ZoneWorldNames.bridgeMesh);
          zoneMesh.channelName(ZoneWorldNames.reportChannel).setWeight(0);
          const opsChannelName = ZoneWorldNames.opsChannel(node.nodeId);
          for (const configuredNodeId of ['zone-node-1', 'zone-node-2']) {
            const configuredChannel = ZoneWorldNames.opsChannel(configuredNodeId);
            const membership = zoneMesh.channelName(configuredChannel);
            if (configuredChannel === opsChannelName) membership.addHandlerGroup('zone-ops');
            else membership.setWeight(0);
          }
          if (node.nodeId === 'zone-node-1') {
            zoneMesh.channelName(ZoneWorldNames.actorsChannel).addHandlerGroup('zone-actors');
          } else zoneMesh.channelName(ZoneWorldNames.actorsChannel).setWeight(0);
          builder.addFanoutChannel(ZoneWorldNames.broadcastChannel)
            .enableSubscriber()
            .addPublishHandler(PacketNames.worldAnnounceEvent, WorldAnnounceSubscriber)
            .addPublishHandler(PacketNames.nodeMaintenanceChangedEvent, MaintenanceChangedSubscriber);
          return {
            ...builder.build(),
            monitoring: {}
          };
        }
      })
    ],
    providers: [
      PlayerActorFactory,
      DeliverZoneNotificationHandler,
      MaintenanceStore,
      NodeRuntimeState,
      PlayerActorRelocationAdapter,
      ZoneEntrySpot,
      ZoneSpot,
      ApplyNodeMaintenanceHandler,
      EnsurePlayerActorHandler,
      GetNodeDiagnosticsHandler,
      MaintenanceChangedSubscriber,
      WorldAnnounceSubscriber,
      EntryEnterWorldHandler,
      EntryJoinWorldHandler,
      PlayerBotTickHandler,
      PlayerMoveHandler,
      PlayerMovement,
      FirstBorderSubscriptionHandler,
      DeliverAnnounceHandler,
      SecondBorderSubscriptionHandler,
      UpdateZonePositionHandler,
      ZoneTickHandler,
      BotTickHandler,
      SpotRuntimeEventHandler
    ]
  })(ZoneNodeModule);
  return ZoneNodeModule;
}

export { createZoneNodeModule };
