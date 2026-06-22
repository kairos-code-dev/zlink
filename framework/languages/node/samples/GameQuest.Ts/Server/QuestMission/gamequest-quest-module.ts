import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames } from '../../Shared/Contracts/messages';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { CollectItemHandler } from './Handlers/collect-item-handler';
import { CompleteMissionHandler } from './Handlers/complete-mission-handler';
import { EnterAreaHandler } from './Handlers/enter-area-handler';
import { KillMonsterHandler } from './Handlers/kill-monster-handler';
import {
  DeleteQuestProjectionHandler,
  GameQuestServerAssertHandler,
  GetGameplaySnapshotHandler,
  GetQuestProgressHandler,
  RebuildQuestProjectionHandler,
  SubscribeQuestHandler,
  SyncQuestProgressHandler,
  UnlockFeatureHandler
} from './Handlers/query-and-self-check-handlers';
import { QuestProgressStore } from './quest-progress-store';

function createGameQuestModule(config: { questEndpoint: string }) {
  class GameQuestModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .codecs()
            .addJson()
          .addClientServerChannel(SampleNames.questChannel)
            .enableServer(config.questEndpoint)
            .addRequestHandler(PacketNames.enterAreaReq, EnterAreaHandler)
            .addRequestHandler(PacketNames.killMonsterReq, KillMonsterHandler)
            .addRequestHandler(PacketNames.collectItemReq, CollectItemHandler)
            .addRequestHandler(PacketNames.completeMissionReq, CompleteMissionHandler)
            .addRequestHandler(PacketNames.unlockFeatureReq, UnlockFeatureHandler)
            .addRequestHandler(PacketNames.subscribeQuestReq, SubscribeQuestHandler)
            .addRequestHandler(PacketNames.getQuestProgressReq, GetQuestProgressHandler)
            .addRequestHandler(PacketNames.syncQuestProgressReq, SyncQuestProgressHandler)
            .addRequestHandler(PacketNames.getGameplaySnapshotReq, GetGameplaySnapshotHandler)
            .addRequestHandler(PacketNames.deleteQuestProjectionReq, DeleteQuestProjectionHandler)
            .addRequestHandler(PacketNames.rebuildQuestProjectionReq, RebuildQuestProjectionHandler)
            .addRequestHandler(PacketNames.gameQuestServerAssertReq, GameQuestServerAssertHandler)
          .build()
      })
    ],
    providers: [
      QuestProgressStore,
      EnterAreaHandler,
      KillMonsterHandler,
      CollectItemHandler,
      CompleteMissionHandler,
      UnlockFeatureHandler,
      SubscribeQuestHandler,
      GetQuestProgressHandler,
      SyncQuestProgressHandler,
      GetGameplaySnapshotHandler,
      DeleteQuestProjectionHandler,
      RebuildQuestProjectionHandler,
      GameQuestServerAssertHandler
    ]
  })(GameQuestModule);

  return GameQuestModule;
}

export {
  createGameQuestModule
};
