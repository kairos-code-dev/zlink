import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_MANAGER,
  ZLINK_SPOT_OUTBOUND
} from '@zlink-systems/nestjs';
import { questMissionSpotRid, SampleNames } from '../../../../Shared/Configuration/sample-names';
import { PlayerQuestSpot } from './Spots/PlayerQuestSpot/player-quest-spot';
import type {
  ZLinkSpotManager,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';

class PlayerQuestSpotProvisioner {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spotHandles: ZLinkSpotManager
  ) {}

  async ensure(playerId: string): Promise<string> {
    const spotRid = questMissionSpotRid(playerId);
    await this.spots.getOrCreate(SampleNames.playerQuestSpotMesh, PlayerQuestSpot, spotRid, { playerId });
    return spotRid;
  }

  async request<TResponse>(playerId: string, request: object): Promise<TResponse> {
    const spotRid = await this.ensure(playerId);
    const spot = await this.spotHandles.find(spotRid);
    if (spot === undefined) {
      throw new Error(`Player quest spot '${spotRid}' was not resolved.`);
    }
    return this.outbound
      .requestToSpot(spot, request)
      .submit<TResponse>();
  }

  async send(playerId: string, message: object): Promise<void> {
    const spotRid = await this.ensure(playerId);
    const spot = await this.spotHandles.find(spotRid);
    if (spot === undefined) {
      throw new Error(`Player quest spot '${spotRid}' was not resolved.`);
    }
    this.outbound.sendToSpot(spot, message).submit();
  }

  async deactivate(playerId: string): Promise<boolean> {
    return await this.spots.close(SampleNames.playerQuestSpotMesh, questMissionSpotRid(playerId));
  }
}

export { PlayerQuestSpotProvisioner };
