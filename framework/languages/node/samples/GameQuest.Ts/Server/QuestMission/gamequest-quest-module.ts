import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { createGameQuestLocationStore, gameQuestLocationOptions } from '../Configuration/location-store';
import { GAMEQUEST_INSTANCE_ID, GAMEQUEST_LOCATION_STORE } from '../Configuration/tokens';
import { GAMEQUEST_SAMPLE_CONFIG, createGameQuestConfigurationModule } from '../Configuration/sample-config';
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

function createQuestMissionModule(instanceId: 'mission-a' | 'mission-b') {
  class GameQuestQuestModule {}
  const routeEndpointKey = instanceId === 'mission-a' ? 'missionAEndpoint' : 'missionBEndpoint';
  const spotEndpointKey = instanceId === 'mission-a' ? 'missionASpotEndpoint' : 'missionBSpotEndpoint';
  const spotRouterEndpointKey = instanceId === 'mission-a'
    ? 'missionASpotRouterEndpoint'
    : 'missionBSpotRouterEndpoint';
  const missionRid = questMissionInstanceRid(instanceId);
  const configuration = createGameQuestConfigurationModule([
    routeEndpointKey,
    spotEndpointKey,
    spotRouterEndpointKey,
    instanceId === 'mission-a' ? 'missionAHttpUrl' : 'missionBHttpUrl',
    'redisEndpoint',
    'redisKeyPrefix',
    'logDir',
    'workDir'
  ]);

  Module({
    imports: [
      configuration,
      ZLinkModule.forRootFactory({
        imports: [configuration],
        inject: [GAMEQUEST_SAMPLE_CONFIG],
        useFactory: (config: GameQuestServerConfig) => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${config.logDir}/flow-${instanceId}.log`)
            .traceLabel(instanceId);
          builder.addLocationStore(createGameQuestLocationStore(config));
          Object.assign(builder.configureLocations(), gameQuestLocationOptions());
          return builder
            .addRouteMeshChannel(SampleNames.questMissionRouteChannel)
              .enableRouter(config[routeEndpointKey])
              .routingId(missionRid)
              .addHandlerGroup('quest-owner')
            .addSpotMesh(SampleNames.playerQuestSpotMesh)
              .enableRouter(config[spotRouterEndpointKey], missionRid)
              .enablePubSub(config[spotEndpointKey], missionRid)
              .addSpotFactory(PlayerQuestSpot)
            .build();
        }
      })
    ],
    providers: [
      { provide: GAMEQUEST_INSTANCE_ID, useValue: instanceId },
      {
        provide: GameplayStateStore,
        inject: [GAMEQUEST_SAMPLE_CONFIG],
        useFactory: (config: GameQuestServerConfig) => new GameplayStateStore(config.workDir)
      },
      {
        provide: QuestEventStore,
        inject: [GAMEQUEST_SAMPLE_CONFIG],
        useFactory: (config: GameQuestServerConfig) => new QuestEventStore(config.workDir)
      },
      {
        provide: QuestReadModelStore,
        inject: [GAMEQUEST_SAMPLE_CONFIG],
        useFactory: (config: GameQuestServerConfig) => new QuestReadModelStore(config.workDir)
      },
      {
        provide: GAMEQUEST_LOCATION_STORE,
        inject: [GAMEQUEST_SAMPLE_CONFIG],
        useFactory: (config: GameQuestServerConfig) => createGameQuestLocationStore(config)
      },
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
