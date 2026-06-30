import { Inject } from '@nestjs/common';
import { QuestEventProcessor } from '../Application/quest-event-processor';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler } from '@zlink-systems/framework';
import type { EventRes, KillMonsterReq } from '../../../Shared/Contracts/messages';

class KillMonsterHandler implements ZLinkRouteRequestHandler<KillMonsterReq, EventRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: KillMonsterReq, context: ZLinkRouteRequestContext): Promise<EventRes> {
    void context;
    return this.processor.killMonster(request);
  }
}

export {
  KillMonsterHandler
};
