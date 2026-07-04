import { SupportUserActorFactory } from '../Actors/support-user-actor-factory';

class EnsureSupportUserActorHandler {
  constructor(private readonly factory = new SupportUserActorFactory()) {}

  handle(actorId: string, role: 'Agent' | 'Customer') {
    return this.factory.create(actorId, role);
  }
}

export { EnsureSupportUserActorHandler };
