import { Inject } from '@nestjs/common';
import { QuestEventProcessor } from '../Application/quest-event-processor';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler } from '@zlink-systems/framework';
import type { CollectItemReq, EventRes } from '../../../Shared/Contracts/messages';

class CollectItemHandler implements ZLinkRouteRequestHandler<CollectItemReq, EventRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: CollectItemReq, context: ZLinkRouteRequestContext): Promise<EventRes> {
    void context;
    return this.processor.collectItem(request);
  }
}

export {
  CollectItemHandler
};
