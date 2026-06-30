import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { QuestOwnerRouter } from '../../Application/quest-owner-router';
import { PlayerQuestSpot } from './Spots/PlayerQuestSpot/player-quest-spot';
import type { ZLinkSpotManager } from '@zlink-systems/framework';

class PlayerQuestSpotProvisioner {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(QuestOwnerRouter) private readonly owners: QuestOwnerRouter
  ) {}

  async ensure(playerId: string): Promise<void> {
    await this.spots.getOrCreate(
      PlayerQuestSpot,
      this.owners.spotRid(playerId),
      { playerId }
    );
  }
}

export {
  PlayerQuestSpotProvisioner
};
