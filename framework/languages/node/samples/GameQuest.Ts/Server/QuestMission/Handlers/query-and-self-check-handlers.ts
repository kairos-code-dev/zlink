import { Inject } from '@nestjs/common';
import { QuestEventProcessor } from '../Application/quest-event-processor';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler } from '@zlink-systems/framework';
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

class SubscribeQuestHandler implements ZLinkRouteRequestHandler<SubscribeQuestReq, SubscribeQuestRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: SubscribeQuestReq, context: ZLinkRouteRequestContext): Promise<SubscribeQuestRes> {
    void context;
    return this.processor.subscribeQuest(request);
  }
}

class UnlockFeatureHandler implements ZLinkRouteRequestHandler<UnlockFeatureReq, EventRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: UnlockFeatureReq, context: ZLinkRouteRequestContext): Promise<EventRes> {
    void context;
    return this.processor.unlockFeature(request);
  }
}

class GetQuestProgressHandler implements ZLinkRouteRequestHandler<GetQuestProgressReq, GetQuestProgressRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: GetQuestProgressReq, context: ZLinkRouteRequestContext): Promise<GetQuestProgressRes> {
    void context;
    return this.processor.getProgress(request);
  }
}

class SyncQuestProgressHandler implements ZLinkRouteRequestHandler<SyncQuestProgressReq, SyncQuestProgressRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: SyncQuestProgressReq, context: ZLinkRouteRequestContext): Promise<SyncQuestProgressRes> {
    void context;
    return this.processor.syncProgress(request);
  }
}

class GetGameplaySnapshotHandler implements ZLinkRouteRequestHandler<GetGameplaySnapshotReq, GetGameplaySnapshotRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: GetGameplaySnapshotReq, context: ZLinkRouteRequestContext): Promise<GetGameplaySnapshotRes> {
    void context;
    return this.processor.getSnapshot(request);
  }
}

class DeleteQuestProjectionHandler implements ZLinkRouteRequestHandler<DeleteQuestProjectionReq, QuestProgress | undefined> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: DeleteQuestProjectionReq, context: ZLinkRouteRequestContext): Promise<QuestProgress | undefined> {
    void context;
    return this.processor.deleteProjection(request);
  }
}

class RebuildQuestProjectionHandler implements ZLinkRouteRequestHandler<RebuildQuestProjectionReq, QuestProgress> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: RebuildQuestProjectionReq, context: ZLinkRouteRequestContext): Promise<QuestProgress> {
    void context;
    return this.processor.rebuildProjection(request);
  }
}

class GameQuestServerAssertHandler implements ZLinkRouteRequestHandler<Record<string, never>, GameQuestServerAssertRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: Record<string, never>, context: ZLinkRouteRequestContext): Promise<GameQuestServerAssertRes> {
    void request;
    void context;
    return this.processor.assertServer();
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
