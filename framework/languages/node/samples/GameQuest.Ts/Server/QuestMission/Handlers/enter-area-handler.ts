import { Inject } from '@nestjs/common';
import { QuestEventProcessor } from '../Application/quest-event-processor';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler } from '@zlink-systems/framework';
import type { EnterAreaReq, GameplayActionRes } from '../../../Shared/Contracts/messages';

class EnterAreaHandler implements ZLinkRouteRequestHandler<EnterAreaReq, GameplayActionRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: EnterAreaReq, context: ZLinkRouteRequestContext): Promise<GameplayActionRes> {
    void context;
    return this.processor.enterArea(request);
  }
}

export {
  EnterAreaHandler
};
