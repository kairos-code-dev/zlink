import { Module } from '@nestjs/common';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
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
import { QuestEventProcessor } from './Application/quest-event-processor';
import { QuestOwnerRouter } from './Application/quest-owner-router';
import { PlayerQuestSpotProvisioner } from './Infrastructure/ZLink/player-quest-spot-provisioner';
import { PlayerQuestSpot } from './Infrastructure/ZLink/Spots/PlayerQuestSpot/player-quest-spot';
import { QuestProgressStore } from '../Shared/Store/quest-progress-store';

function createGameQuestModule(config: {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
}, missionEndpoint: string, instanceId: string) {
  class GameQuestModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder.configureDispatch()
            .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .traceLogFile(`${process.env.GAMEQUEST_LOG_DIR ?? 'logs'}/flow-${instanceId}.log`)
            .traceLabel(instanceId);
          return builder
          .useDiscovery()
            .addRegistryEndpoint(config.registryRouterEndpoint)
          .addRouteMesh(SampleNames.questMissionRouteChannel)
            .enableRouter(missionEndpoint)
            .routingId(instanceId)
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
          .addSpotMesh(SampleNames.questMissionRouteChannel)
            .addSpotFactory(PlayerQuestSpot)
          .build();
        }
      })
    ],
    providers: [
      QuestProgressStore,
      QuestOwnerRouter,
      PlayerQuestSpotProvisioner,
      PlayerQuestSpot,
      QuestEventProcessor,
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
