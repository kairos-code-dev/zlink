import { Inject } from '@nestjs/common';
import { QuestProgressStore } from '../quest-progress-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { EventRes, KillMonsterReq } from '../../../Shared/Contracts/messages';

class KillMonsterHandler implements ZLinkRequestHandler<KillMonsterReq, EventRes> {
  constructor(@Inject(QuestProgressStore) private readonly quests: QuestProgressStore) {}

  async handle(request: KillMonsterReq): Promise<EventRes> {
    return this.quests.killMonster(request);
  }
}

export {
  KillMonsterHandler
};
