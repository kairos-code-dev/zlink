import { Inject } from '@nestjs/common';
import { QuestProgressStore } from '../quest-progress-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { CompleteMissionReq, EventRes } from '../../../Shared/Contracts/messages';

class CompleteMissionHandler implements ZLinkRequestHandler<CompleteMissionReq, EventRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: CompleteMissionReq): Promise<EventRes> {
    return this.quests.completeMission(request);
  }
}

export {
  CompleteMissionHandler
};
