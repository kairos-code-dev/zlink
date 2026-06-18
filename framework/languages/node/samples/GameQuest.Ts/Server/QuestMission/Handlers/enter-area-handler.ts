import { Inject } from '@nestjs/common';
import { QuestProgressStore } from '../quest-progress-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { EnterAreaReq, EventRes } from '../../../Shared/Contracts/messages';

class EnterAreaHandler implements ZLinkRequestHandler<EnterAreaReq, EventRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: EnterAreaReq): Promise<EventRes> {
    return this.quests.enterArea(request);
  }
}

export {
  EnterAreaHandler
};
