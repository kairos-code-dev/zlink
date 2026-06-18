import { Inject } from '@nestjs/common';
import { QuestProgressStore } from '../quest-progress-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { CollectItemReq, EventRes } from '../../../Shared/Contracts/messages';

class CollectItemHandler implements ZLinkRequestHandler<CollectItemReq, EventRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: CollectItemReq): Promise<EventRes> {
    return this.quests.collectItem(request);
  }
}

export {
  CollectItemHandler
};
