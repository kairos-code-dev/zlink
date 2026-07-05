import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { createGameQuestLocationStore, gameQuestLocationOptions } from '../Configuration/location-store';
import { GameplayActionService } from './Application/gameplay-action-service';
import { GameplayEventPublisher } from './Infrastructure/ZLink/gameplay-event-publisher';
import { GameQuestSessionFactory } from './game-api-session';
import { QuestProgressStore } from '../Shared/Store/quest-progress-store';
import type { GameQuestServerConfig } from '../Configuration/sample-config';

function createGameApiModule(config: GameQuestServerConfig, instanceId: 'api-a' | 'api-b') {
  class GameApiModule {}
  const endpoint = instanceId === 'api-a' ? config.apiARouteEndpoint : config.apiBRouteEndpoint;
  const streamEndpoint = instanceId === 'api-a' ? config.apiAStreamEndpoint : config.apiBStreamEndpoint;

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
              .enableRouter(endpoint)
              .routingId(instanceId)
            .addStreamNode(SampleNames.playerStreamNode)
              .bind(streamEndpoint)
              .registerSession(GameQuestSessionFactory)
            .build();
        }
      })
    ],
    providers: [
      { provide: QuestProgressStore, useFactory: () => new QuestProgressStore(config.workDir) },
      GameplayActionService,
      GameplayEventPublisher,
      GameQuestSessionFactory
    ]
  })(GameApiModule);

  return GameApiModule;
}

export { createGameApiModule };
