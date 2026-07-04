import { SupportUserActor } from './support-user-actor';

class SupportUserActorFactory {
  create(actorId: string, role: 'Agent' | 'Customer'): SupportUserActor {
    return new SupportUserActor(actorId, role);
  }
}

export { SupportUserActorFactory };
