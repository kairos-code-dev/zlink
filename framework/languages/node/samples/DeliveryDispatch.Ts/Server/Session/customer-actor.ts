import type { ZLinkActor, ZLinkActorContext, ZLinkActorFactory, ZLinkMessage } from '@zlink-systems/framework';

class CustomerActor implements ZLinkActor {
  private readonly deliveries = new Set<string>();
  private joined = false;

  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {}

  subscribe(deliveryId: string): void {
    this.deliveries.add(deliveryId);
  }

  accepts(deliveryId: string): boolean {
    return this.deliveries.has(deliveryId);
  }

  async ensureJoined(spotRid: string, request: ZLinkMessage): Promise<void> {
    if (this.joined) return;
    const joined = await this.context.joinEntrySpot(spotRid, request).submit();
    if (joined.status !== 'accepted') {
      throw new Error(`Customer actor '${this.actorId}' could not join its entry spot.`);
    }
    this.joined = true;
  }
}

class CustomerActorFactory implements ZLinkActorFactory {
  private static directory: CustomerActorDirectory | undefined;

  static useDirectory(directory: CustomerActorDirectory): void {
    CustomerActorFactory.directory = directory;
  }

  async create(actorId: string, context: ZLinkActorContext): Promise<CustomerActor> {
    const actor = new CustomerActor(actorId, context);
    CustomerActorFactory.directory?.set(actor);
    return actor;
  }
}

class CustomerActorDirectory {
  private readonly actors = new Map<string, CustomerActor>();

  set(actor: CustomerActor): void {
    this.actors.set(actor.actorId, actor);
  }

  require(actorId: string): CustomerActor {
    const actor = this.actors.get(actorId);
    if (actor === undefined) throw new Error(`Customer actor '${actorId}' is not active.`);
    return actor;
  }
}

export { CustomerActor, CustomerActorDirectory, CustomerActorFactory };
