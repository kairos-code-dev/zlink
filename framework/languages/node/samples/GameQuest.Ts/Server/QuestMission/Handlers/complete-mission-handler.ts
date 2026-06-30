import { Inject } from '@nestjs/common';
import { QuestEventProcessor } from '../Application/quest-event-processor';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler } from '@zlink-systems/framework';
import type { CompleteMissionReq, GameplayActionRes } from '../../../Shared/Contracts/messages';

class CompleteMissionHandler implements ZLinkRouteRequestHandler<CompleteMissionReq, GameplayActionRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: CompleteMissionReq, context: ZLinkRouteRequestContext): Promise<GameplayActionRes> {
    void context;
    return this.processor.completeMission(request);
  }
}

export {
  CompleteMissionHandler
};
