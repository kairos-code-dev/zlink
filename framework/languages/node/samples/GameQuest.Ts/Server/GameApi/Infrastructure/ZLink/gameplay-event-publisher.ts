import { Inject } from '@nestjs/common';
import { ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { applyGameplayEventReq, PacketNames } from '../../../../Shared/Contracts/messages';
import { questMissionRouteRid, SampleNames } from '../../../../Shared/Configuration/sample-names';
import type { ZLinkRouteClient } from '@zlink-systems/framework';
import type { ApplyGameplayEventRes, GameplayEventEnvelope } from '../../../../Shared/Contracts/messages';

class GameplayEventPublisher {
  constructor(@Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient) {}

  async request<TResponse = ApplyGameplayEventRes>(event: GameplayEventEnvelope): Promise<TResponse> {
    return await this.routes
      .requestToNode(SampleNames.questMissionRouteChannel, questMissionRouteRid(event.playerId), applyGameplayEventReq(event))
      .packetName(PacketNames.applyGameplayEventReq)
      .timeout(SampleNames.requestTimeout)
      .submit<TResponse>();
  }
}

export { GameplayEventPublisher };
