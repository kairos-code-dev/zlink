import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../Shared/Contracts/messages';
import { QuestEventProcessor } from '../../Application/quest-event-processor';
import { QuestOwnerRouter } from '../../Application/quest-owner-router';
import { PlayerQuestSpotProvisioner } from './player-quest-spot-provisioner';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { ApplyGameplayEventReq, ApplyGameplayEventRes } from '../../../../Shared/Contracts/messages';

@zlinkRequestHandler('quest-mission', PacketNames.applyGameplayEventReq)
class GameplayEventRouteHandler implements ZLinkRequestHandler<ApplyGameplayEventReq, ApplyGameplayEventRes> {
  constructor(
    private readonly processor: QuestEventProcessor,
    private readonly ownerRouter: QuestOwnerRouter,
    private readonly spots: PlayerQuestSpotProvisioner
  ) {}

  async handle(request: ApplyGameplayEventReq): Promise<ApplyGameplayEventRes> {
    if (!this.ownerRouter.isLocalOwner(request.event.playerId)) {
      return { applied: false, projection: [] };
    }
    await this.spots.ensure(request.event.playerId);
    return await this.processor.process(request.event);
  }
}

export { GameplayEventRouteHandler };
