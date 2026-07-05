import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../Shared/Contracts/messages';
import { QuestEventProcessor } from '../../Application/quest-event-processor';
import { QuestOwnerRouter } from '../../Application/quest-owner-router';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { ApplyGameplayEventReq, ApplyGameplayEventRes } from '../../../../Shared/Contracts/messages';

@zlinkRequestHandler('quest-owner', PacketNames.applyGameplayEventReq)
class GameplayEventRouteHandler implements ZLinkRequestHandler<ApplyGameplayEventReq, ApplyGameplayEventRes> {
  constructor(
    private readonly processor: QuestEventProcessor,
    private readonly ownerRouter: QuestOwnerRouter
  ) {}

  async handle(request: ApplyGameplayEventReq): Promise<ApplyGameplayEventRes> {
    if (!this.ownerRouter.isLocalOwner(request.event.playerId)) {
      return { applied: false, projection: [] };
    }
    return await this.processor.process(request.event);
  }
}

export { GameplayEventRouteHandler };
