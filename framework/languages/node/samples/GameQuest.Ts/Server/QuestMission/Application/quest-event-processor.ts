import { Inject } from '@nestjs/common';
import { QuestProgressStore } from '../../Shared/Store/quest-progress-store';
import { PlayerQuestSpotProvisioner } from '../Infrastructure/ZLink/player-quest-spot-provisioner';
import type {
  CollectItemReq,
  CompleteMissionReq,
  DeleteQuestProjectionReq,
  EnterAreaReq,
  GameplayActionRes,
  GameQuestServerAssertRes,
  GetGameplaySnapshotReq,
  GetGameplaySnapshotRes,
  GetQuestProgressReq,
  GetQuestProgressRes,
  KillMonsterReq,
  QuestProgress,
  RebuildQuestProjectionReq,
  SubscribeQuestReq,
  SubscribeQuestRes,
  SyncQuestProgressReq,
  SyncQuestProgressRes,
  UnlockFeatureReq
} from '../../../Shared/Contracts/messages';

class QuestEventProcessor {
  constructor(
    @Inject(QuestProgressStore) private readonly quests: QuestProgressStore,
    @Inject(PlayerQuestSpotProvisioner) private readonly spots: PlayerQuestSpotProvisioner
  ) {}

  async enterArea(request: EnterAreaReq): Promise<GameplayActionRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.enterArea(request);
  }

  async killMonster(request: KillMonsterReq): Promise<GameplayActionRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.killMonster(request);
  }

  async collectItem(request: CollectItemReq): Promise<GameplayActionRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.collectItem(request);
  }

  async completeMission(request: CompleteMissionReq): Promise<GameplayActionRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.completeMission(request);
  }

  async unlockFeature(request: UnlockFeatureReq): Promise<GameplayActionRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.unlockFeature(request);
  }

  async subscribeQuest(request: SubscribeQuestReq): Promise<SubscribeQuestRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.subscribeQuest(request.playerId);
  }

  async getProgress(request: GetQuestProgressReq): Promise<GetQuestProgressRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.getProgress(request.playerId);
  }

  async syncProgress(request: SyncQuestProgressReq): Promise<SyncQuestProgressRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.syncProgress(request.playerId);
  }

  async getSnapshot(request: GetGameplaySnapshotReq): Promise<GetGameplaySnapshotRes> {
    await this.spots.ensure(request.playerId);
    return this.quests.getSnapshot(request.playerId);
  }

  async deleteProjection(request: DeleteQuestProjectionReq): Promise<QuestProgress | undefined> {
    await this.spots.ensure(request.playerId);
    return this.quests.deleteProjection(request);
  }

  async rebuildProjection(request: RebuildQuestProjectionReq): Promise<QuestProgress> {
    await this.spots.ensure(request.playerId);
    return this.quests.rebuildProjection(request);
  }

  assertServer(): GameQuestServerAssertRes {
    return this.quests.assertServer();
  }
}

export {
  QuestEventProcessor
};
