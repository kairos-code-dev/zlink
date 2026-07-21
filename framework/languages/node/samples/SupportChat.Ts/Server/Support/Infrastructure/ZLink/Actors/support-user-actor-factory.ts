import { Injectable } from '@nestjs/common';
import { SupportUserActor } from './support-user-actor';
import type { ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';

@Injectable()
class SupportUserActorFactory implements ZLinkActorFactory {
  async create(actorId: string, context: ZLinkActorContext): Promise<SupportUserActor> {
    return new SupportUserActor(actorId, context);
  }
}

export { SupportUserActorFactory };
