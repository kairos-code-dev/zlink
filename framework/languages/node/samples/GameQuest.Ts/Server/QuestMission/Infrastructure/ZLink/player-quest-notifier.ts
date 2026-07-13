import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT } from '@zlink-systems/nestjs';
import {
  QuestCompletedNotify,
  QuestProgressNotify
} from '../../../../Shared/Contracts/messages';
import { GAMEQUEST_LOCATION_STORE } from '../../../Configuration/tokens';
import type {
  ZLinkActorClient,
  ZLinkLocationStore
} from '@zlink-systems/framework';
import type { QuestProgress } from '../../../../Shared/Contracts/messages';

class PlayerQuestNotifier {
  constructor(
    @Inject(GAMEQUEST_LOCATION_STORE) private readonly locations: ZLinkLocationStore,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient
  ) {}

  async notify(playerId: string, progress: QuestProgress[], completedQuestIds: string[]): Promise<void> {
    if (progress.length === 0) return;
    const actor = (await this.locations.resolveActor({ actorId: playerId }))?.actorRef;
    if (actor === undefined) {
      console.error(`gamequest notification skipped: no bound actor location player=${playerId}`);
      return;
    }
    for (const changed of progress) {
      this.actors.sendToActor(actor, new QuestProgressNotify(playerId, changed)).submit();
      if (completedQuestIds.includes(changed.questId)) {
        this.actors.sendToActor(
          actor,
          new QuestCompletedNotify(playerId, changed, true)
        ).submit();
      }
    }
  }
}

export { PlayerQuestNotifier };
