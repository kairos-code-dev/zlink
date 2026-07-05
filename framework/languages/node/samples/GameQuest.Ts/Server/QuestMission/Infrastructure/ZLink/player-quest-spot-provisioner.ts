import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { questMissionSpotRid } from '../../../../Shared/Configuration/sample-names';
import { PlayerQuestSpot } from './Spots/PlayerQuestSpot/player-quest-spot';
import type { ZLinkSpotManager } from '@zlink-systems/framework';

class PlayerQuestSpotProvisioner {
  constructor(@Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager) {}

  async ensure(playerId: string): Promise<string> {
    const spotRid = questMissionSpotRid(playerId);
    await this.spots.getOrCreate(PlayerQuestSpot, spotRid, { playerId });
    return spotRid;
  }
}

export { PlayerQuestSpotProvisioner };
