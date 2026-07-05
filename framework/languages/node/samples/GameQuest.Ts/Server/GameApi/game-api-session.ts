import { Inject } from '@nestjs/common';
import { GameplayActionService } from './Application/gameplay-action-service';
import { QuestProgressStore } from '../Shared/Store/quest-progress-store';
import {
  PacketNames,
  QuestStatuses
} from '../../Shared/Contracts/messages';
import type {
  CollectItemReq,
  CompleteMissionReq,
  EnterAreaReq,
  GetQuestProgressReq,
  JoinSessionReq,
  KillMonsterReq,
  QuestCompletedNotify,
  QuestProgress,
  QuestProgressNotify,
  SyncQuestProgressReq,
  UnlockFeatureReq
} from '../../Shared/Contracts/messages';
import type {
  ZLinkMessage,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';

class GameQuestSession implements ZLinkSession {
  private playerId?: string;

  constructor(
    readonly context: ZLinkSessionContext,
    private readonly store: QuestProgressStore,
    private readonly actions: GameplayActionService
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName === PacketNames.joinSessionReq) {
      const request = payload.decode<JoinSessionReq>(Object as never);
      this.playerId = request.playerId;
      this.context.client.reply({ activeQuests: this.store.readProjection(request.playerId) }).submit();
      return;
    }
    if (dispatch.packetName === PacketNames.getQuestProgressReq) {
      const request = payload.decode<GetQuestProgressReq>(Object as never);
      this.context.client.reply({ activeQuests: this.store.readProjection(request.playerId) }).submit();
      return;
    }
    if (dispatch.packetName === PacketNames.syncQuestProgressReq) {
      const request = payload.decode<SyncQuestProgressReq>(Object as never);
      const synced = await this.actions.sync(request.playerId);
      await this.notify(request.playerId, synced.updatedQuests);
      this.context.client.reply(synced).submit();
      return;
    }
    if (dispatch.packetName === PacketNames.killMonsterReq) {
      const result = await this.actions.killMonster(payload.decode<KillMonsterReq>(Object as never));
      await this.replyAndNotify(result.response, result.projection, result.completedQuestId);
      return;
    }
    if (dispatch.packetName === PacketNames.collectItemReq) {
      const result = await this.actions.collectItem(payload.decode<CollectItemReq>(Object as never));
      await this.replyAndNotify(result.response, result.projection, result.completedQuestId);
      return;
    }
    if (dispatch.packetName === PacketNames.completeMissionReq) {
      const result = await this.actions.completeMission(payload.decode<CompleteMissionReq>(Object as never));
      await this.replyAndNotify(result.response, result.projection, result.completedQuestId);
      return;
    }
    if (dispatch.packetName === PacketNames.enterAreaReq) {
      const result = await this.actions.enterArea(payload.decode<EnterAreaReq>(Object as never));
      await this.replyAndNotify(result.response, result.projection, result.completedQuestId);
      return;
    }
    if (dispatch.packetName === PacketNames.unlockFeatureReq) {
      const result = await this.actions.unlockFeature(payload.decode<UnlockFeatureReq>(Object as never));
      await this.replyAndNotify(result.response, result.projection, result.completedQuestId);
      return;
    }
    throw new Error(`Unsupported GameQuest packet '${dispatch.packetName}'.`);
  }

  private async replyAndNotify(response: unknown, projection: QuestProgress[], completedQuestId?: string): Promise<void> {
    this.context.client.reply(response).submit();
    if (projection.length > 0) {
      await this.notify(projection[0].playerId, projection, completedQuestId);
    }
  }

  private async notify(playerId: string, projection: QuestProgress[], completedQuestId?: string): Promise<void> {
    if (this.playerId !== playerId) {
      return;
    }
    if (projection.length === 0) {
      return;
    }
    const latest = projection[projection.length - 1];
    this.context.client.send({
      playerId,
      targetConnectionId: this.context.sessionId,
      progress: latest
    } satisfies QuestProgressNotify).packetName(PacketNames.questProgressNotify).submit();
    if (completedQuestId !== undefined || latest.status === QuestStatuses.RewardGranted) {
      const completed = projection.find((progress) => progress.questId === completedQuestId) ?? latest;
      this.context.client.send({
        playerId,
        targetConnectionId: this.context.sessionId,
        progress: completed,
        rewardGranted: true
      } satisfies QuestCompletedNotify).packetName(PacketNames.questCompletedNotify).submit();
    }
  }
}

class GameQuestSessionFactory implements ZLinkSessionFactory<GameQuestSession> {
  constructor(
    @Inject(QuestProgressStore) private readonly store: QuestProgressStore,
    private readonly actions: GameplayActionService
  ) {}

  create(context: ZLinkSessionContext): GameQuestSession {
    return new GameQuestSession(context, this.store, this.actions);
  }
}

export { GameQuestSession, GameQuestSessionFactory };
