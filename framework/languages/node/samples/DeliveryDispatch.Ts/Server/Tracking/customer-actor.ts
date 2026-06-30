import type { ZLinkActor, ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';

class CustomerActor implements ZLinkActor {
  readonly context!: ZLinkActorContext;

  constructor(readonly actorId: string) {}
}

class CustomerActorFactory implements ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext): CustomerActor {
    const actor = new CustomerActor(actorId);
    Object.defineProperty(actor, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });
    return actor;
  }
}

export {
  CustomerActor,
  CustomerActorFactory
};
