import {
  areaEntered,
  featureUnlocked,
  itemCollected,
  missionCompleted,
  monsterKilled
} from '../Domain/gameplay-domain';
import { getQuestProgressReq } from '../../../Shared/Contracts/messages';
import { PlayerSessionDirectory } from '../player-session-directory';
import { GameplayEventPublisher } from '../Infrastructure/ZLink/gameplay-event-publisher';
import type {
  CollectItemReq,
  CompleteMissionReq,
  EnterAreaReq,
  GameplayActionRes,
  GetQuestProgressRes,
  KillMonsterReq,
  UnlockFeatureReq
} from '../../../Shared/Contracts/messages';

class GameplayActionService {
  constructor(
    private readonly publisher: GameplayEventPublisher,
    private readonly sessions: PlayerSessionDirectory
  ) {}

  async killMonster(request: KillMonsterReq): Promise<GameplayActionRes> {
    return this.publishAndNotify(monsterKilled(request), request.playerId);
  }

  async collectItem(request: CollectItemReq): Promise<GameplayActionRes> {
    return this.publishAndNotify(itemCollected(request), request.playerId);
  }

  async completeMission(request: CompleteMissionReq): Promise<GameplayActionRes> {
    return this.publishAndNotify(missionCompleted(request), request.playerId);
  }

  async enterArea(request: EnterAreaReq): Promise<GameplayActionRes> {
    return this.publishAndNotify(areaEntered(request), request.playerId);
  }

  async unlockFeature(request: UnlockFeatureReq): Promise<GameplayActionRes> {
    return this.publishAndNotify(featureUnlocked(request), request.playerId);
  }

  private async publishAndNotify(payload: unknown, playerId: string): Promise<GameplayActionRes> {
    const result = await this.publisher.publish<GameplayActionRes>(payload);
    const progress = await this.publisher.request<GetQuestProgressRes>(getQuestProgressReq(playerId));
    await this.sessions.publish(playerId, progress.activeQuests);
    return result;
  }
}

export {
  GameplayActionService
};
