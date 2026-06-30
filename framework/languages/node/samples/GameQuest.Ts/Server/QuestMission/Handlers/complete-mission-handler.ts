import { Inject } from '@nestjs/common';
import { QuestEventProcessor } from '../Application/quest-event-processor';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler } from '@zlink-systems/framework';
import type { CompleteMissionReq, EventRes } from '../../../Shared/Contracts/messages';

class CompleteMissionHandler implements ZLinkRouteRequestHandler<CompleteMissionReq, EventRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: CompleteMissionReq, context: ZLinkRouteRequestContext): Promise<EventRes> {
    void context;
    return this.processor.completeMission(request);
  }
}

export {
  CompleteMissionHandler
};
