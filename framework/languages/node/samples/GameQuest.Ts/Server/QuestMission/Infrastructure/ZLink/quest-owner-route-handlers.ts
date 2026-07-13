import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import {
  DeleteQuestProjectionReq,
  GetQuestProgressReq,
  PacketNames,
  RebuildQuestProjectionReq,
  SyncQuestProgressReq
} from '../../../../Shared/Contracts/messages';
import { PlayerQuestSpotProvisioner } from './player-quest-spot-provisioner';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  DeleteQuestProjectionRes,
  GetQuestProgressRes,
  QuestProgress,
  SyncQuestProgressRes
} from '../../../../Shared/Contracts/messages';

@zlinkRequestHandler('quest-owner', PacketNames.getQuestProgressReq)
class GetQuestProgressRouteHandler implements ZLinkRequestHandler<GetQuestProgressReq, GetQuestProgressRes> {
  constructor(private readonly playerQuests: PlayerQuestSpotProvisioner) {}

  async handle(request: GetQuestProgressReq): Promise<GetQuestProgressRes> {
    return await this.playerQuests.request<GetQuestProgressRes>(
      request.playerId,
      new GetQuestProgressReq(request.playerId)
    );
  }
}

@zlinkRequestHandler('quest-owner', PacketNames.syncQuestProgressReq)
class SyncQuestProgressRouteHandler implements ZLinkRequestHandler<SyncQuestProgressReq, SyncQuestProgressRes> {
  constructor(private readonly playerQuests: PlayerQuestSpotProvisioner) {}

  async handle(request: SyncQuestProgressReq): Promise<SyncQuestProgressRes> {
    return await this.playerQuests.request<SyncQuestProgressRes>(
      request.playerId,
      new SyncQuestProgressReq(request.playerId)
    );
  }
}

@zlinkRequestHandler('quest-owner', PacketNames.deleteQuestProjectionReq)
class DeleteQuestProjectionRouteHandler implements ZLinkRequestHandler<DeleteQuestProjectionReq, DeleteQuestProjectionRes> {
  constructor(private readonly playerQuests: PlayerQuestSpotProvisioner) {}

  async handle(request: DeleteQuestProjectionReq): Promise<DeleteQuestProjectionRes> {
    return await this.playerQuests.request<DeleteQuestProjectionRes>(
      request.playerId,
      new DeleteQuestProjectionReq(request.playerId, request.questId)
    );
  }
}

@zlinkRequestHandler('quest-owner', PacketNames.rebuildQuestProjectionReq)
class RebuildQuestProjectionRouteHandler implements ZLinkRequestHandler<RebuildQuestProjectionReq, QuestProgress> {
  constructor(private readonly playerQuests: PlayerQuestSpotProvisioner) {}

  async handle(request: RebuildQuestProjectionReq): Promise<QuestProgress> {
    return await this.playerQuests.request<QuestProgress>(
      request.playerId,
      new RebuildQuestProjectionReq(request.playerId, request.questId)
    );
  }
}

export {
  DeleteQuestProjectionRouteHandler,
  GetQuestProgressRouteHandler,
  RebuildQuestProjectionRouteHandler,
  SyncQuestProgressRouteHandler
};
