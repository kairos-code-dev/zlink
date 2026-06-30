import { Inject } from '@nestjs/common';
import { QuestEventProcessor } from '../Application/quest-event-processor';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler } from '@zlink-systems/framework';
import type { GameplayActionRes, KillMonsterReq } from '../../../Shared/Contracts/messages';

class KillMonsterHandler implements ZLinkRouteRequestHandler<KillMonsterReq, GameplayActionRes> {
  constructor(@Inject(QuestEventProcessor) private readonly processor: QuestEventProcessor) {}

  async handle(request: KillMonsterReq, context: ZLinkRouteRequestContext): Promise<GameplayActionRes> {
    void context;
    return this.processor.killMonster(request);
  }
}

export {
  KillMonsterHandler
};
