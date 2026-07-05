import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../Shared/Contracts/messages';
import { QuestProgressStore } from '../../../Shared/Store/quest-progress-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  DeleteQuestProjectionReq,
  DeleteQuestProjectionRes,
  GetQuestProgressReq,
  GetQuestProgressRes,
  QuestProgress,
  RebuildQuestProjectionReq,
  SyncQuestProgressReq,
  SyncQuestProgressRes
} from '../../../../Shared/Contracts/messages';

@zlinkRequestHandler('quest-owner', PacketNames.getQuestProgressReq)
class GetQuestProgressRouteHandler implements ZLinkRequestHandler<GetQuestProgressReq, GetQuestProgressRes> {
  constructor(@Inject(QuestProgressStore) private readonly store: QuestProgressStore) {}

  async handle(request: GetQuestProgressReq): Promise<GetQuestProgressRes> {
    return { activeQuests: this.store.readProjection(request.playerId) };
  }
}

@zlinkRequestHandler('quest-owner', PacketNames.syncQuestProgressReq)
class SyncQuestProgressRouteHandler implements ZLinkRequestHandler<SyncQuestProgressReq, SyncQuestProgressRes> {
  constructor(@Inject(QuestProgressStore) private readonly store: QuestProgressStore) {}

  async handle(request: SyncQuestProgressReq): Promise<SyncQuestProgressRes> {
    return { updatedQuests: this.store.syncProgress(request.playerId) };
  }
}

@zlinkRequestHandler('quest-owner', PacketNames.deleteQuestProjectionReq)
class DeleteQuestProjectionRouteHandler implements ZLinkRequestHandler<DeleteQuestProjectionReq, DeleteQuestProjectionRes> {
  constructor(@Inject(QuestProgressStore) private readonly store: QuestProgressStore) {}

  async handle(request: DeleteQuestProjectionReq): Promise<DeleteQuestProjectionRes> {
    this.store.deleteProjection(request.playerId, request.questId);
    return { deleted: true };
  }
}

@zlinkRequestHandler('quest-owner', PacketNames.rebuildQuestProjectionReq)
class RebuildQuestProjectionRouteHandler implements ZLinkRequestHandler<RebuildQuestProjectionReq, QuestProgress> {
  constructor(@Inject(QuestProgressStore) private readonly store: QuestProgressStore) {}

  async handle(request: RebuildQuestProjectionReq): Promise<QuestProgress> {
    void request.count;
    return this.store.rebuildProjection(request.playerId, request.questId);
  }
}

export {
  DeleteQuestProjectionRouteHandler,
  GetQuestProgressRouteHandler,
  RebuildQuestProjectionRouteHandler,
  SyncQuestProgressRouteHandler
};
