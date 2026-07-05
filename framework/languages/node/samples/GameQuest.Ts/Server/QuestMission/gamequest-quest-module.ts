import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { PacketNames } from '../../Shared/Contracts/messages';
import { createGameQuestLocationStore, gameQuestLocationOptions } from '../Configuration/location-store';
import { QuestProgressStore } from '../Shared/Store/quest-progress-store';
import { QuestEventProcessor } from './Application/quest-event-processor';
import { QuestOwnerRouter } from './Application/quest-owner-router';
import { GameplayEventRouteHandler } from './Infrastructure/ZLink/gameplay-event-route-handler';
import { PlayerQuestSpotProvisioner } from './Infrastructure/ZLink/player-quest-spot-provisioner';
import { PlayerQuestSpot } from './Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot';
import type { GameQuestServerConfig } from '../Configuration/sample-config';

function createQuestMissionModule(config: GameQuestServerConfig, instanceId: 'mission-a' | 'mission-b') {
  class GameQuestQuestModule {}
  const missionEndpoint = instanceId === 'mission-a' ? config.missionAEndpoint : config.missionBEndpoint;
  const spotRouter = instanceId === 'mission-a' ? config.missionASpotRouterEndpoint : config.missionBSpotRouterEndpoint;
  const spotEndpoint = instanceId === 'mission-a' ? config.missionASpotEndpoint : config.missionBSpotEndpoint;

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.GAMEQUEST_LOG_DIR ?? 'logs'}/flow-${instanceId}.log`)
            .traceLabel(instanceId);
          builder.addLocationStore(createGameQuestLocationStore(config));
          Object.assign(builder.configureLocations(), gameQuestLocationOptions());
          return builder
            .addRouteMeshChannel(SampleNames.questMissionRouteChannel)
              .enableRouter(missionEndpoint)
              .routingId(instanceId)
              .addHandlerGroup('quest-mission')
              .addRequestHandler(PacketNames.applyGameplayEventReq, GameplayEventRouteHandler)
              .addRequestHandler(PacketNames.enterAreaReq, GameplayEventRouteHandler)
              .addRequestHandler(PacketNames.killMonsterReq, GameplayEventRouteHandler)
              .addRequestHandler(PacketNames.collectItemReq, GameplayEventRouteHandler)
              .addRequestHandler(PacketNames.completeMissionReq, GameplayEventRouteHandler)
            .addSpotMesh(SampleNames.playerQuestSpotMesh)
              .enableRouter(spotRouter, instanceId)
              .enablePubSub(spotEndpoint, instanceId)
              .addSpotFactory(PlayerQuestSpot)
            .build();
        }
      })
    ],
    providers: [
      { provide: QuestProgressStore, useFactory: () => new QuestProgressStore(config.workDir) },
      { provide: QuestOwnerRouter, useFactory: () => new QuestOwnerRouter(instanceId) },
      QuestEventProcessor,
      PlayerQuestSpotProvisioner,
      GameplayEventRouteHandler
    ]
  })(GameQuestQuestModule);

  return GameQuestQuestModule;
}

export { createQuestMissionModule };
