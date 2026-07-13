import type { ZLinkActor, ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';

class CustomerActor implements ZLinkActor {
  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {}
}

class CustomerActorFactory implements ZLinkActorFactory {
  async create(actorId: string, context: ZLinkActorContext): Promise<CustomerActor> {
    return new CustomerActor(actorId, context);
  }
}

export {
  CustomerActor,
  CustomerActorFactory
};
