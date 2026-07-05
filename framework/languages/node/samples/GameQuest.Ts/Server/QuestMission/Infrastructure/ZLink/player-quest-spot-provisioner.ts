import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { questMissionSpotRid } from '../../../../Shared/Configuration/sample-names';
import type { ZLinkSpotManager } from '@zlink-systems/framework';

class PlayerQuestSpotProvisioner {
  constructor(@Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager) {}

  async ensure(playerId: string): Promise<string> {
    const spotRid = questMissionSpotRid(playerId);
    void this.spots;
    return spotRid;
  }
}

export { PlayerQuestSpotProvisioner };
