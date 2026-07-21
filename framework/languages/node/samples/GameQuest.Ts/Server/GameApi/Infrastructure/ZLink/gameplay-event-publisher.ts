import { Inject } from '@nestjs/common';
import { ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { gameplayMsg } from '../../../../Shared/Contracts/messages';
import { questMissionRouteChannel, SampleNames } from '../../../../Shared/Configuration/sample-names';
import type { ZLinkRouteClient } from '@zlink-systems/framework';
import type { GameplayEventEnvelope } from '../../../../Shared/Contracts/messages';

class GameplayEventPublisher {
  constructor(
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient
  ) {}

  async send(event: GameplayEventEnvelope): Promise<void> {
    const message = gameplayMsg(event);
    this.routes
      .sendToChannel(SampleNames.playerQuestSpotMesh, questMissionRouteChannel(event.playerId), message)
      .submit();
  }
}

export { GameplayEventPublisher };
