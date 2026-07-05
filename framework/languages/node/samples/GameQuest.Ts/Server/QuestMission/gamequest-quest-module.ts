import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { PacketNames } from '../../Shared/Contracts/messages';
import { questMissionInstanceRid } from '../../Shared/Configuration/sample-names';
import { createGameQuestLocationStore, gameQuestLocationOptions } from '../Configuration/location-store';
import { QuestProgressStore } from '../Shared/Store/quest-progress-store';
import { QuestEventProcessor } from './Application/quest-event-processor';
import { QuestOwnerRouter } from './Application/quest-owner-router';
import { GameplayEventRouteHandler } from './Infrastructure/ZLink/gameplay-event-route-handler';
import {
  DeleteQuestProjectionRouteHandler,
  GetQuestProgressRouteHandler,
  RebuildQuestProjectionRouteHandler,
  SyncQuestProgressRouteHandler
} from './Infrastructure/ZLink/quest-owner-route-handlers';
import type { GameQuestServerConfig } from '../Configuration/sample-config';

function createQuestMissionModule(config: GameQuestServerConfig, instanceId: 'mission-a' | 'mission-b') {
  class GameQuestQuestModule {}
  const missionEndpoint = instanceId === 'mission-a' ? config.missionAEndpoint : config.missionBEndpoint;
  const missionRid = questMissionInstanceRid(instanceId);

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
              .routingId(missionRid)
              .addHandlerGroup('quest-owner')
              .addRequestHandler(PacketNames.applyGameplayEventReq, GameplayEventRouteHandler)
              .addRequestHandler(PacketNames.getQuestProgressReq, GetQuestProgressRouteHandler)
              .addRequestHandler(PacketNames.syncQuestProgressReq, SyncQuestProgressRouteHandler)
              .addRequestHandler(PacketNames.deleteQuestProjectionReq, DeleteQuestProjectionRouteHandler)
              .addRequestHandler(PacketNames.rebuildQuestProjectionReq, RebuildQuestProjectionRouteHandler)
            .build();
        }
      })
    ],
    providers: [
      { provide: QuestProgressStore, useFactory: () => new QuestProgressStore(config.workDir) },
      { provide: QuestOwnerRouter, useFactory: () => new QuestOwnerRouter(missionRid) },
      QuestEventProcessor,
      GameplayEventRouteHandler,
      GetQuestProgressRouteHandler,
      SyncQuestProgressRouteHandler,
      DeleteQuestProjectionRouteHandler,
      RebuildQuestProjectionRouteHandler
    ]
  })(GameQuestQuestModule);

  return GameQuestQuestModule;
}

export { createQuestMissionModule };
