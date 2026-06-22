import { Injectable } from '@nestjs/common';
import type { ZLinkPublishContext, ZLinkSpotSubscriptionHandler } from '@zlink-systems/framework';
import type { BingoRewardAcquiredEvent } from '../../../../../../Shared/Contracts/messages';
import type { BingoRoomSpot } from '../bingo-room-spot';

@Injectable()
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
