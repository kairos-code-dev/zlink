import { Module } from '@nestjs/common';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { createGameQuestLocationStore, gameQuestLocationOptions } from '../Configuration/location-store';
import { GAMEQUEST_LOCATION_STORE } from '../Configuration/tokens';
import { GameplayActionService } from './Application/gameplay-action-service';
import { GameplayEventPublisher } from './Infrastructure/ZLink/gameplay-event-publisher';
import { GameQuestEntrySpot } from './Infrastructure/ZLink/gamequest-entry-spot';
import { GameQuestPlayerActor } from './Infrastructure/ZLink/gamequest-player-actor';
import {
  CollectItemHandler,
  CompleteMissionHandler,
  EnterAreaHandler,
  GetQuestProgressHandler,
  KillMonsterHandler,
  SyncQuestProgressHandler,
  UnlockFeatureHandler
} from './Infrastructure/ZLink/gamequest-player-handlers';
import {
  QuestCompletedNotificationHandler,
  QuestProgressNotificationHandler
} from './Infrastructure/ZLink/quest-notification-handlers';
import { GameQuestSessionFactory } from './game-api-session';
import {
  GameQuestSelfCheckStore,
  GameplayStateStore,
  QuestEventStore,
  QuestReadModelStore
} from '../Shared/Store/quest-progress-store';
import type { ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';
import type { GameQuestServerConfig } from '../Configuration/sample-config';

class GameQuestPlayerActorFactory implements ZLinkActorFactory {
  async create(actorId: string, context: ZLinkActorContext): Promise<GameQuestPlayerActor> {
    return new GameQuestPlayerActor(actorId, context);
  }
}

function createGameApiModule(config: GameQuestServerConfig, instanceId: 'api-a' | 'api-b') {
  class GameApiModule {}
  const locationStore = createGameQuestLocationStore(config);
  const streamEndpoint = instanceId === 'api-a' ? config.apiAStreamEndpoint : config.apiBStreamEndpoint;
  const actorSpotEndpoint = instanceId === 'api-a' ? config.apiAActorSpotEndpoint : config.apiBActorSpotEndpoint;
  const apiRid = instanceId === 'api-a' ? 'gamequest-api-a' : 'gamequest-api-b';

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
              .enableClient()
            .addStreamNode(SampleNames.playerStreamNode)
              .bind(streamEndpoint)
              .registerSession(GameQuestSessionFactory)
            .addSpotMesh(SampleNames.playerQuestSpotMesh)
              .enableRouter(actorSpotEndpoint, apiRid)
              .addEntrySpot(GameQuestEntrySpot)
              .actorFactory(SampleNames.playerActorType, GameQuestPlayerActorFactory)
            .build();
        }
      })
    ],
    providers: [
      { provide: GameplayStateStore, useFactory: () => new GameplayStateStore(config.workDir) },
      { provide: QuestEventStore, useFactory: () => new QuestEventStore(config.workDir) },
      { provide: QuestReadModelStore, useFactory: () => new QuestReadModelStore(config.workDir) },
      {
        provide: GameQuestSelfCheckStore,
        useFactory: (gameplay: GameplayStateStore, events: QuestEventStore, readModel: QuestReadModelStore) =>
          new GameQuestSelfCheckStore(gameplay, events, readModel),
        inject: [GameplayStateStore, QuestEventStore, QuestReadModelStore]
      },
      { provide: GAMEQUEST_LOCATION_STORE, useValue: locationStore },
      GameplayActionService,
      GameplayEventPublisher,
      GameQuestEntrySpot,
      GameQuestPlayerActorFactory,
      QuestProgressNotificationHandler,
      QuestCompletedNotificationHandler,
      GameQuestSessionFactory,
      KillMonsterHandler,
      CollectItemHandler,
      CompleteMissionHandler,
      EnterAreaHandler,
      UnlockFeatureHandler,
      GetQuestProgressHandler,
      SyncQuestProgressHandler
    ]
  })(GameApiModule);

  return GameApiModule;
}

export { createGameApiModule };
