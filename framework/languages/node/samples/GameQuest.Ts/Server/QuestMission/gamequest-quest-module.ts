import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { createGameQuestLocationStore, gameQuestLocationOptions } from '../Configuration/location-store';
import { GAMEQUEST_LOCATION_STORE } from '../Configuration/tokens';
import {
  GameplayStateStore,
  QuestEventStore,
  QuestReadModelStore
} from '../Shared/Store/quest-progress-store';
import { questMissionInstanceRid, SampleNames } from '../../Shared/Configuration/sample-names';
import { QuestEventProcessor } from './Application/quest-event-processor';
import { QuestOwnerRouter } from './Application/quest-owner-router';
import { GameplayEventRouteHandler } from './Infrastructure/ZLink/gameplay-event-route-handler';
import { PlayerQuestNotifier } from './Infrastructure/ZLink/player-quest-notifier';
import { PlayerQuestSpotProvisioner } from './Infrastructure/ZLink/player-quest-spot-provisioner';
import { PlayerQuestSpot } from './Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot';
import {
  ApplyGameplayEventSpotHandler,
  DeleteQuestProjectionSpotHandler,
  GetQuestProgressSpotHandler,
  RebuildQuestProjectionSpotHandler,
  SyncQuestProgressSpotHandler
} from './Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot-handlers';
import {
  DeleteQuestProjectionRouteHandler,
  GetQuestProgressRouteHandler,
  RebuildQuestProjectionRouteHandler,
  SyncQuestProgressRouteHandler
} from './Infrastructure/ZLink/quest-owner-route-handlers';
import type { GameQuestServerConfig } from '../Configuration/sample-config';

function createQuestMissionModule(config: GameQuestServerConfig, instanceId: 'mission-a' | 'mission-b') {
  class GameQuestQuestModule {}
  const routeEndpoint = instanceId === 'mission-a' ? config.missionAEndpoint : config.missionBEndpoint;
  const spotEndpoint = instanceId === 'mission-a' ? config.missionASpotEndpoint : config.missionBSpotEndpoint;
  const spotRouterEndpoint = instanceId === 'mission-a'
    ? config.missionASpotRouterEndpoint
    : config.missionBSpotRouterEndpoint;
  const missionRid = questMissionInstanceRid(instanceId);
  const locationStore = createGameQuestLocationStore(config);

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.GAMEQUEST_LOG_DIR ?? 'logs'}/flow-${instanceId}.log`)
            .traceLabel(instanceId);
          builder.addLocationStore(locationStore);
          Object.assign(builder.configureLocations(), gameQuestLocationOptions());
          return builder
            .addRouteMeshChannel(SampleNames.questMissionRouteChannel)
              .enableRouter(routeEndpoint)
              .routingId(missionRid)
              .addHandlerGroup('quest-owner')
            .addSpotMesh(SampleNames.playerQuestSpotMesh)
              .enableRouter(spotRouterEndpoint, missionRid)
              .enablePubSub(spotEndpoint, missionRid)
              .addSpotFactory(PlayerQuestSpot)
            .build();
        }
      })
    ],
    providers: [
      { provide: GameplayStateStore, useFactory: () => new GameplayStateStore(config.workDir) },
      { provide: QuestEventStore, useFactory: () => new QuestEventStore(config.workDir) },
      { provide: QuestReadModelStore, useFactory: () => new QuestReadModelStore(config.workDir) },
      { provide: GAMEQUEST_LOCATION_STORE, useValue: locationStore },
      { provide: QuestOwnerRouter, useFactory: () => new QuestOwnerRouter(missionRid) },
      QuestEventProcessor,
      PlayerQuestNotifier,
      PlayerQuestSpotProvisioner,
      GameplayEventRouteHandler,
      GetQuestProgressRouteHandler,
      SyncQuestProgressRouteHandler,
      DeleteQuestProjectionRouteHandler,
      RebuildQuestProjectionRouteHandler,
      ApplyGameplayEventSpotHandler,
      GetQuestProgressSpotHandler,
      SyncQuestProgressSpotHandler,
      DeleteQuestProjectionSpotHandler,
      RebuildQuestProjectionSpotHandler
    ]
  })(GameQuestQuestModule);

  return GameQuestQuestModule;
}

export { createQuestMissionModule };
