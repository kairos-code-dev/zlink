import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_OUTBOUND
} from '@zlink-systems/nestjs';
import { questMissionSpotId, SampleNames } from '../../../../Shared/Configuration/sample-names';
import { DeactivatePlayerQuestSpotReq } from '../../../../Shared/Contracts/messages';
import type {
  ZLinkSpotOutbound
} from '@zlink-systems/framework';

class PlayerQuestSpotProvisioner {
  constructor(
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound
  ) {}

  async request<TResponse>(playerId: string, request: object): Promise<TResponse> {
    return this.outbound
      .requestToSpot(questMissionSpotId(playerId), request)
      .instanceSpot(SampleNames.playerQuestSpotType)
      .inMesh(SampleNames.playerQuestSpotMesh)
      .submit<TResponse>();
  }

  async send(playerId: string, message: object): Promise<void> {
    await this.outbound
      .sendToSpot(questMissionSpotId(playerId), message)
      .instanceSpot(SampleNames.playerQuestSpotType)
      .inMesh(SampleNames.playerQuestSpotMesh)
      .submit();
  }

  async deactivate(playerId: string): Promise<boolean> {
    const result = await this.request<{ closed: boolean }>(
      playerId,
      new DeactivatePlayerQuestSpotReq(playerId)
    );
    return result.closed;
  }
}

export { PlayerQuestSpotProvisioner };
