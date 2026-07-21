import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT } from '@zlink-systems/nestjs';
import {
  QuestCompletedNotify,
  QuestProgressNotify
} from '../../../../Shared/Contracts/messages';
import { SampleNames } from '../../../../Shared/Configuration/sample-names';
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
    const actor = (await this.locations.resolveActor({
      meshName: SampleNames.playerQuestSpotMesh,
      actorId: playerId
    }))?.actorRef;
    if (actor === undefined) {
      console.error(`gamequest notification skipped: no bound actor location player=${playerId}`);
      return;
    }
    for (const changed of progress) {
      await this.actors.sendToActor(
        SampleNames.playerQuestSpotMesh,
        actor,
        new QuestProgressNotify(playerId, changed)
      ).submit();
      if (completedQuestIds.includes(changed.questId)) {
        await this.actors.sendToActor(
          SampleNames.playerQuestSpotMesh,
          actor,
          new QuestCompletedNotify(playerId, changed, true)
        ).submit();
      }
    }
  }
}

export { PlayerQuestNotifier };
