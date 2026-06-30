import { Inject } from '@nestjs/common';
import { QuestEventProcessor } from '../Application/quest-event-processor';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler } from '@zlink-systems/framework';
import type { EnterAreaReq, EventRes } from '../../../Shared/Contracts/messages';

class EnterAreaHandler implements ZLinkRouteRequestHandler<EnterAreaReq, EventRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: EnterAreaReq, context: ZLinkRouteRequestContext): Promise<EventRes> {
    void context;
    return this.processor.enterArea(request);
  }
}

export {
  EnterAreaHandler
};
