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
import { questMissionInstanceChannel, SampleNames } from '../../Shared/Configuration/sample-names';
import { QuestEventProcessor } from './Application/quest-event-processor';
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
  const spotRouterEndpointKey = instanceId === 'mission-a'
    ? 'missionASpotRouterEndpoint'
    : 'missionBSpotRouterEndpoint';
  const configuration = createGameQuestConfigurationModule([
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
          gameQuestLocationOptions(builder.configureLocations());
          const spotMesh = builder.addRouteMesh(SampleNames.playerQuestSpotMesh)
            .listen(config[spotRouterEndpointKey])
            .setRoutingIdPrefix('gamequest-mission')
            .addSpotFactory(PlayerQuestSpot);
          spotMesh.channelName(questMissionInstanceChannel(instanceId)).addHandlerGroup('quest-owner');
          spotMesh.channelName(SampleNames.playerQuestSpotMesh);
          return builder.build();
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
