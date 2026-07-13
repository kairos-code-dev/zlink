import { Injectable } from '@nestjs/common';
import { zlinkSpotSubscriptionHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import { BingoRoomSpot } from '../bingo-room-spot';
import type { ZLinkPublishContext, ZLinkSpotSubscriptionHandler } from '@zlink-systems/framework';
import type { BingoRewardAcquiredEvent } from '../../../../../../../Shared/Contracts/messages';

@Injectable()
@zlinkSpotSubscriptionHandler({
  spot: () => BingoRoomSpot,
  topic: SampleNames.roomRewardTopic
})
class BingoRewardAcquiredEventHandler
  implements ZLinkSpotSubscriptionHandler<BingoRoomSpot, BingoRewardAcquiredEvent> {
  async handle(
    room: BingoRoomSpot,
    event: BingoRewardAcquiredEvent,
    context: ZLinkPublishContext
  ): Promise<void> {
    void context;
    await room.announceReward(event);
  }
}

export { BingoRewardAcquiredEventHandler };
