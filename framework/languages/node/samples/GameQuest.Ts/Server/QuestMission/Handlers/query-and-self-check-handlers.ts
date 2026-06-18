import { Inject } from '@nestjs/common';
import { QuestProgressStore } from '../quest-progress-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  DeleteQuestProjectionReq,
  GameQuestServerAssertRes,
  GetGameplaySnapshotReq,
  GetGameplaySnapshotRes,
  GetQuestProgressReq,
  GetQuestProgressRes,
  QuestProgress,
  RebuildQuestProjectionReq,
  SubscribeQuestReq,
  SubscribeQuestRes,
  SyncQuestProgressReq,
  SyncQuestProgressRes,
  UnlockFeatureReq,
  EventRes
} from '../../../Shared/Contracts/messages';

class SubscribeQuestHandler implements ZLinkRequestHandler<SubscribeQuestReq, SubscribeQuestRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: SubscribeQuestReq): Promise<SubscribeQuestRes> {
    return this.quests.subscribeQuest(request.playerId);
  }
}

class UnlockFeatureHandler implements ZLinkRequestHandler<UnlockFeatureReq, EventRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: UnlockFeatureReq): Promise<EventRes> {
    return this.quests.unlockFeature(request);
  }
}

class GetQuestProgressHandler implements ZLinkRequestHandler<GetQuestProgressReq, GetQuestProgressRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: GetQuestProgressReq): Promise<GetQuestProgressRes> {
    return this.quests.getProgress(request.playerId);
  }
}

class SyncQuestProgressHandler implements ZLinkRequestHandler<SyncQuestProgressReq, SyncQuestProgressRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: SyncQuestProgressReq): Promise<SyncQuestProgressRes> {
    return this.quests.syncProgress(request.playerId);
  }
}

class GetGameplaySnapshotHandler implements ZLinkRequestHandler<GetGameplaySnapshotReq, GetGameplaySnapshotRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: GetGameplaySnapshotReq): Promise<GetGameplaySnapshotRes> {
    return this.quests.getSnapshot(request.playerId);
  }
}

class DeleteQuestProjectionHandler implements ZLinkRequestHandler<DeleteQuestProjectionReq, QuestProgress | undefined> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: DeleteQuestProjectionReq): Promise<QuestProgress | undefined> {
    return this.quests.deleteProjection(request);
  }
}

class RebuildQuestProjectionHandler implements ZLinkRequestHandler<RebuildQuestProjectionReq, QuestProgress> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: RebuildQuestProjectionReq): Promise<QuestProgress> {
    return this.quests.rebuildProjection(request);
  }
}

class GameQuestServerAssertHandler implements ZLinkRequestHandler<Record<string, never>, GameQuestServerAssertRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(): Promise<GameQuestServerAssertRes> {
    return this.quests.assertServer();
  }
}

export {
  DeleteQuestProjectionHandler,
  GameQuestServerAssertHandler,
  GetGameplaySnapshotHandler,
  GetQuestProgressHandler,
  RebuildQuestProjectionHandler,
  SubscribeQuestHandler,
  SyncQuestProgressHandler,
  UnlockFeatureHandler
};
