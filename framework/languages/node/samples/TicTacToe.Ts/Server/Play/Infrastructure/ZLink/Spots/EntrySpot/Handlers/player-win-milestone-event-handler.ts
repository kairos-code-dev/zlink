import { Injectable } from '@nestjs/common';
import type { ZLinkPublishContext, ZLinkSpotSubscriptionHandler } from '@zlink-systems/framework';
import type { PlayerWinMilestoneMsg } from '../../../../../../../Shared/Contracts/messages';
import type { PlayEntrySpot } from '../play-entry-spot';

@Injectable()
class PlayerWinMilestoneEventHandler
  implements ZLinkSpotSubscriptionHandler<PlayEntrySpot, PlayerWinMilestoneMsg> {
  async handle(
    entrySpot: PlayEntrySpot,
    event: PlayerWinMilestoneMsg,
    context: ZLinkPublishContext
  ): Promise<void> {
    void context;
    await entrySpot.notifyMilestone(event);
  }
}

export { PlayerWinMilestoneEventHandler };
