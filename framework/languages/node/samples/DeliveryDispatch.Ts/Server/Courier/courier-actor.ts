import type { ZLinkActor, ZLinkActorContext, ZLinkActorFactory } from '@zlink-systems/framework';

class CourierActor implements ZLinkActor {
  readonly context!: ZLinkActorContext;

  constructor(readonly actorId: string) {}
}

class CourierActorFactory implements ZLinkActorFactory {
  async create(actorId: string, context: ZLinkActorContext): Promise<CourierActor> {
    const actor = new CourierActor(actorId);
    Object.defineProperty(actor, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });
    return actor;
  }
}

export {
  CourierActor,
  CourierActorFactory
};
