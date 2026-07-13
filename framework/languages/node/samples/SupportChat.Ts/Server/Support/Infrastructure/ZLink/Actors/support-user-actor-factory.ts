import { Injectable } from '@nestjs/common';
import { SupportActorDirectory } from './support-actor-directory';
import { SupportUserActor } from './support-user-actor';
import type { ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';

@Injectable()
class SupportUserActorFactory implements ZLinkActorFactory {
  constructor(private readonly actors: SupportActorDirectory) {}

  async create(actorId: string, context: ZLinkActorContext): Promise<SupportUserActor> {
    return this.actors.bind(new SupportUserActor(actorId, context));
  }
}

export { SupportUserActorFactory };
