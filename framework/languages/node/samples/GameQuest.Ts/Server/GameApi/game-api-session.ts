import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { GameplayActionService } from './Application/gameplay-action-service';
import { QuestProgressStore } from '../Shared/Store/quest-progress-store';
import { questMissionRouteRid, SampleNames } from '../../Shared/Configuration/sample-names';
import {
  PacketNames,
  QuestStatuses,
  syncQuestProgressReq
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
  SyncQuestProgressRes,
  UnlockFeatureReq
} from '../../Shared/Contracts/messages';
import type {
  ZLinkActorManager,
  ZLinkMessage,
  ZLinkRouteClient,
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
    private readonly actions: GameplayActionService,
    private readonly routes: ZLinkRouteClient,
    private readonly actorManager: ZLinkActorManager
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName === PacketNames.joinSessionReq) {
      const request = payload.decode<JoinSessionReq>(Object as never);
      const actorRef = await this.actorManager.getOrCreate(request.playerId, SampleNames.playerActorType);
      await this.context.actors.bindOrGet(actorRef);
      this.playerId = request.playerId;
      const synced = await this.syncProjection(request.playerId);
      this.store.mergeProjection(request.playerId, synced.updatedQuests);
      this.context.client.reply({ activeQuests: synced.updatedQuests }).submit();
      return;
    }
    if (dispatch.packetName === PacketNames.getQuestProgressReq) {
      const request = payload.decode<GetQuestProgressReq>(Object as never);
      const synced = await this.syncProjection(request.playerId);
      this.store.mergeProjection(request.playerId, synced.updatedQuests);
      this.context.client.reply({ activeQuests: synced.updatedQuests }).submit();
      return;
    }
    if (dispatch.packetName === PacketNames.syncQuestProgressReq) {
      const request = payload.decode<SyncQuestProgressReq>(Object as never);
      const synced = await this.syncProjection(request.playerId);
      this.store.mergeProjection(request.playerId, synced.updatedQuests);
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

  private async syncProjection(playerId: string): Promise<SyncQuestProgressRes> {
    return await this.routes
      .requestToNode(SampleNames.questMissionRouteChannel, questMissionRouteRid(playerId), syncQuestProgressReq(playerId))
      .packetName(PacketNames.syncQuestProgressReq)
      .timeout(SampleNames.requestTimeout)
      .submit<SyncQuestProgressRes>();
  }
}

class GameQuestSessionFactory implements ZLinkSessionFactory<GameQuestSession> {
  constructor(
    @Inject(QuestProgressStore) private readonly store: QuestProgressStore,
    private readonly actions: GameplayActionService,
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager
  ) {}

  create(context: ZLinkSessionContext): GameQuestSession {
    return new GameQuestSession(context, this.store, this.actions, this.routes, this.actorManager);
  }
}

export { GameQuestSession, GameQuestSessionFactory };
